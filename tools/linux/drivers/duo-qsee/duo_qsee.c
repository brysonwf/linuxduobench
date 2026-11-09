// SPDX-License-Identifier: GPL-2.0-only
/*
 * duo_qsee - Surface Duo secure-world (QSEE / TrustZone) driver, work in progress
 * (2026-11-08; notes: secure-world.md).
 *
 * Step 1 (always): ask the TrustZone OS which trusted applications (TAs) are loaded, by name - a lookup, nothing is
 * started (qcom_scm_qseecom_app_get_id).
 * Step 2 (parameters): load what Android would load at runtime, from the phone's OWN firmware (modem_a, linked under
 * /lib/firmware/qcom/sm8150/microsoft by the boot scripts) - nothing is shipped:
 *   services=cmnlib,cmnlib64   the TA common libraries (QSEE "services image"; needed before most TAs)
 *   app=fpctzrel               a TA, started; its app ID is logged
 * An image = <name>.mdt (ELF header + program headers) followed by <name>.b00 ... .bNN (one file per program header,
 * in order), the layout the QSEE loader expects (qcom_scm_qseecom_services_load / _app_start, our qcom_scm patch).
 * Needs the kernel patch "firmware: qcom: scm: add QSEECOM image loading".
 */
#include <linux/debugfs.h>
#include <linux/elf.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/firmware/qcom/qcom_tzmem.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

static char *names[16];
static int nr_names;
module_param_array(names, charp, &nr_names, 0444);
MODULE_PARM_DESC(names, "extra trusted-application names to look up");
static char *services[4];
static int nr_services;
module_param_array(services, charp, &nr_services, 0444);
MODULE_PARM_DESC(services, "QSEE services images to load first (e.g. cmnlib,cmnlib64)");
static char *app;
module_param(app, charp, 0444);
MODULE_PARM_DESC(app, "trusted application to load and start (e.g. fpctzrel)");
static char *fwdir = "qcom/sm8150/microsoft";
module_param(fwdir, charp, 0444);
MODULE_PARM_DESC(fwdir, "firmware directory of the phone's own QSEE images");

static bool fpc_info;
module_param(fpc_info, bool, 0444);
MODULE_PARM_DESC(fpc_info, "with app=fpctzrel: ask the fingerprint TA for its build info (step 3)");

static struct platform_device *pdev;	/* only a struct device for request_firmware */
static u32 started_app;

static const char *const known[] = {
	"fpctzrel", "fpctzapp", "fingerpr", "keymaster", "keymaster64", "km4", "gatekeeper",
	"cmnlib", "cmnlib64", "qsee_ipc_irq_spss", "spss", "hdcp2p2", "hdcpsrm", "widevine", "voicepri",
	"uefisecapp", "qcom.tz.uefisecapp", "smplap32", "smplap64", "gptest", "secotaap",
};

static void lookup(const char *name)
{
	u32 id = 0;
	int ret = qcom_scm_qseecom_app_get_id(name, &id);

	if (!ret)
		pr_info("duo_qsee: %-20s LOADED, app id %u\n", name, id);
	else if (ret == -ENOENT)
		pr_info("duo_qsee: %-20s not loaded\n", name);
	else
		pr_info("duo_qsee: %-20s lookup error %d\n", name, ret);
}

/* <name>.mdt + .b00..bNN -> one buffer; returns its size, *mdt_len = the .mdt part */
static ssize_t read_image(const char *name, void **out, size_t *mdt_len)
{
	const struct firmware *mdt, *seg;
	char path[128];
	unsigned int i, phnum;
	size_t total;
	u8 *buf;
	int ret;

	snprintf(path, sizeof(path), "%s/%s.mdt", fwdir, name);
	ret = request_firmware(&mdt, path, &pdev->dev);
	if (ret) {
		pr_err("duo_qsee: no %s (%d)\n", path, ret);
		return ret;
	}
	if (mdt->size < EI_NIDENT || memcmp(mdt->data, ELFMAG, SELFMAG)) {
		pr_err("duo_qsee: %s is not an ELF\n", path);
		release_firmware(mdt);
		return -EINVAL;
	}
	phnum = mdt->data[EI_CLASS] == ELFCLASS64 ? ((const struct elf64_hdr *)mdt->data)->e_phnum
						  : ((const struct elf32_hdr *)mdt->data)->e_phnum;
	total = mdt->size;
	for (i = 0; i < phnum; i++) {
		snprintf(path, sizeof(path), "%s/%s.b%02u", fwdir, name, i);
		if (!request_firmware(&seg, path, &pdev->dev)) {
			total += seg->size;
			release_firmware(seg);
		}
	}
	buf = vmalloc(total);
	if (!buf) {
		release_firmware(mdt);
		return -ENOMEM;
	}
	memcpy(buf, mdt->data, mdt->size);
	*mdt_len = mdt->size;
	total = mdt->size;
	release_firmware(mdt);
	for (i = 0; i < phnum; i++) {
		snprintf(path, sizeof(path), "%s/%s.b%02u", fwdir, name, i);
		if (request_firmware(&seg, path, &pdev->dev))
			continue;	/* a segment without file data (p_filesz 0) has no .bNN */
		memcpy(buf + total, seg->data, seg->size);
		total += seg->size;
		release_firmware(seg);
	}
	pr_info("duo_qsee: %s: %u segments, mdt %zu + image %zu bytes\n", name, phnum, *mdt_len, total);
	*out = buf;
	return total;
}

/*
 * Step 3: one command to the FPC fingerprint TA, the simplest its Android HAL sends (fpc_tee_get_build_info, read from
 * the HAL's code): the TA command lives in a shared buffer (Android: ION QSECOM heap, page-sized):
 *   u32 group = 0x0c, u32 cmd = 0x01, i32 status (out), u32 0, u32 size = 1024, char info[1024] (out)
 * and the QSEECOM request (64 bytes) carries u32 shared_size (page-rounded), u32 shared_phys (QSEECom patches it in at
 * offset 4: "modified cmd"); the 64-byte response starts with the transfer status (0 = ok).
 */
static void fpc_build_info(u32 app_id)
{
	struct qcom_tzmem_pool_config cfg = { .policy = QCOM_TZMEM_POLICY_STATIC, .initial_size = 4 * PAGE_SIZE };
	struct qcom_tzmem_pool *pool;
	u32 *req, *rsp, *sh;
	phys_addr_t sp;
	int ret;

	pool = devm_qcom_tzmem_pool_new(&pdev->dev, &cfg);
	if (IS_ERR(pool)) {
		pr_err("duo_qsee: tzmem pool: %ld\n", PTR_ERR(pool));
		return;
	}
	req = qcom_tzmem_alloc(pool, 64, GFP_KERNEL);
	rsp = qcom_tzmem_alloc(pool, 64, GFP_KERNEL);
	sh = qcom_tzmem_alloc(pool, PAGE_SIZE, GFP_KERNEL);
	if (!req || !rsp || !sh)
		goto out;
	memset(req, 0, 64);
	memset(rsp, 0, 64);
	memset(sh, 0, PAGE_SIZE);
	sh[0] = 0x0c;
	sh[1] = 0x01;
	sh[4] = 1024;
	sp = qcom_tzmem_to_phys(sh);
	if (sp > U32_MAX) {
		pr_err("duo_qsee: shared buffer at %pa is above 4 GiB - the TA takes a 32-bit address, not sending\n", &sp);
		goto out;
	}
	req[0] = PAGE_SIZE;
	req[1] = (u32)sp;
	ret = qcom_scm_qseecom_app_send(app_id, req, 64, rsp, 64);
	pr_info("duo_qsee: fpc build info: send %d, transfer status %d, TA status %d\n", ret, (s32)rsp[0], (s32)sh[2]);
	if (!ret && !rsp[0] && !sh[2])
		pr_info("duo_qsee: fpc build info: \"%.*s\"\n", 200, (char *)&sh[5]);
	else
		pr_info("duo_qsee: rsp %*ph | shared %*ph\n", 16, rsp, 32, sh);
out:
	qcom_tzmem_free(sh);
	qcom_tzmem_free(rsp);
	qcom_tzmem_free(req);
}

/*
 * Developer interface (debugfs, root only): /sys/kernel/debug/duo_qsee/cmd talks to the app this module started.
 * write: the shared-buffer command (<= one page; the rest is zeroed) -> sent with the 64-byte request
 * {u32 page size, u32 shared phys}; read: the 64-byte transfer response followed by the whole shared page.
 */
static struct qcom_tzmem_pool *dbg_pool;
static u32 *dbg_req, *dbg_rsp, *dbg_sh;
static int dbg_ret;
static DEFINE_MUTEX(dbg_lock);
static struct dentry *dbg_dir;

static ssize_t cmd_write(struct file *f, const char __user *ubuf, size_t n, loff_t *pos)
{
	phys_addr_t sp;

	if (!started_app || n > PAGE_SIZE)
		return -EINVAL;
	mutex_lock(&dbg_lock);
	memset(dbg_req, 0, 64);
	memset(dbg_rsp, 0, 64);
	memset(dbg_sh, 0, PAGE_SIZE);
	if (copy_from_user(dbg_sh, ubuf, n)) {
		mutex_unlock(&dbg_lock);
		return -EFAULT;
	}
	sp = qcom_tzmem_to_phys(dbg_sh);
	dbg_req[0] = PAGE_SIZE;
	dbg_req[1] = (u32)sp;
	dbg_ret = qcom_scm_qseecom_app_send(started_app, dbg_req, 64, dbg_rsp, 64);
	pr_info("duo_qsee: cmd %08x/%08x (%zu bytes): send %d, transfer status %d\n", dbg_sh[0], dbg_sh[1], n,
		dbg_ret, (s32)dbg_rsp[0]);
	mutex_unlock(&dbg_lock);
	return dbg_ret ? dbg_ret : n;
}

static ssize_t cmd_read(struct file *f, char __user *ubuf, size_t n, loff_t *pos)
{
	ssize_t r;

	mutex_lock(&dbg_lock);
	if (*pos < 64)
		r = simple_read_from_buffer(ubuf, n, pos, dbg_rsp, 64);
	else {
		loff_t p = *pos - 64;

		r = simple_read_from_buffer(ubuf, n, &p, dbg_sh, PAGE_SIZE);
		if (r > 0)
			*pos += r;
	}
	mutex_unlock(&dbg_lock);
	return r;
}

static const struct file_operations cmd_fops = {
	.owner = THIS_MODULE, .write = cmd_write, .read = cmd_read, .llseek = default_llseek,
};

/*
 * /sys/kernel/debug/duo_qsee/raw: a plain request/response to ANY loaded app (e.g. keymaster64, app id 1, which the
 * boot chain started): write u32 app_id + the request (<= 1 KiB); read the 1 KiB response.
 */
static u32 *raw_req, *raw_rsp;

static ssize_t raw_write(struct file *f, const char __user *ubuf, size_t n, loff_t *pos)
{
	u32 app_id;
	int ret;

	if (n < 8 || n > 4 + 1024)
		return -EINVAL;
	if (copy_from_user(&app_id, ubuf, 4))
		return -EFAULT;
	mutex_lock(&dbg_lock);
	memset(raw_req, 0, 1024);
	memset(raw_rsp, 0, 1024);
	if (copy_from_user(raw_req, ubuf + 4, n - 4)) {
		mutex_unlock(&dbg_lock);
		return -EFAULT;
	}
	/* exact request length; a 960-byte response area like the Android HAL's keymaster call (64 + 960 = its 1 KiB) */
	ret = qcom_scm_qseecom_app_send(app_id, raw_req, n - 4, raw_rsp, 960);
	pr_info("duo_qsee: raw app %u cmd %08x: send %d, rsp %08x\n", app_id, raw_req[0], ret, raw_rsp[0]);
	mutex_unlock(&dbg_lock);
	return ret ? ret : n;
}

static ssize_t raw_read(struct file *f, char __user *ubuf, size_t n, loff_t *pos)
{
	ssize_t r;

	mutex_lock(&dbg_lock);
	r = simple_read_from_buffer(ubuf, n, pos, raw_rsp, 1024);
	mutex_unlock(&dbg_lock);
	return r;
}

static const struct file_operations raw_fops = {
	.owner = THIS_MODULE, .write = raw_write, .read = raw_read, .llseek = default_llseek,
};

static void debugfs_setup(void)
{
	/* 5 buffers (cmd req/rsp/shared, raw req/rsp); tzmem rounds each up to a page */
	struct qcom_tzmem_pool_config cfg = { .policy = QCOM_TZMEM_POLICY_STATIC, .initial_size = 8 * PAGE_SIZE };
	phys_addr_t sp;

	dbg_pool = devm_qcom_tzmem_pool_new(&pdev->dev, &cfg);
	if (IS_ERR(dbg_pool))
		return;
	dbg_req = qcom_tzmem_alloc(dbg_pool, 64, GFP_KERNEL);
	dbg_rsp = qcom_tzmem_alloc(dbg_pool, 64, GFP_KERNEL);
	dbg_sh = qcom_tzmem_alloc(dbg_pool, PAGE_SIZE, GFP_KERNEL);
	if (!dbg_req || !dbg_rsp || !dbg_sh)
		return;
	sp = qcom_tzmem_to_phys(dbg_sh);
	if (sp > U32_MAX)
		return;
	dbg_dir = debugfs_create_dir("duo_qsee", NULL);
	debugfs_create_file("cmd", 0600, dbg_dir, NULL, &cmd_fops);
	raw_req = qcom_tzmem_alloc(dbg_pool, 1024, GFP_KERNEL);
	raw_rsp = qcom_tzmem_alloc(dbg_pool, 1024, GFP_KERNEL);
	if (raw_req && raw_rsp)
		debugfs_create_file("raw", 0600, dbg_dir, NULL, &raw_fops);
	pr_info("duo_qsee: /sys/kernel/debug/duo_qsee/cmd talks to app id %u\n", started_app);
}

/*
 * QSEECOM listeners (step 5, logging only): register the given IDs with a shared buffer each (the sizes Android's
 * qseecomd uses) and log every request a trusted application sends them - then answer "failure". This shows which
 * service a TA call needs and the request format, before any storage is implemented.
 *   listeners=0xa,0x2000      (0xa = files, 0x2000 = RPMB, 0x7000 = GP files, 0xb = time - secure-world.md)
 */
static unsigned int listeners[8];
static int nr_listeners;
module_param_array(listeners, uint, &nr_listeners, 0444);
MODULE_PARM_DESC(listeners, "QSEECOM listener IDs to register (logging only)");

static struct qcom_tzmem_pool *lst_pool;
static void *lst_buf[8];
static bool lst_ok[8];

static size_t listener_size(u32 id)
{
	switch (id) {
	case 0x7000: return 516096;
	case 0x2000: return 25600;
	default: return 20480;
	}
}

/*
 * Userspace listener daemon channel: /dev/duo_qsee_listener (root). One request at a time (QSEECOM calls are
 * serialised by qcom_scm's call lock, under which this handler runs).
 *   read():  blocks until a TA asks a listener; returns u32 listener_id, u32 buffer size, then the shared buffer
 *   write(): u32 listener_id, s32 status (0 = served), then the reply bytes (copied over the shared buffer's head)
 * Without a daemon (or an answer within 10 s) the handler answers failure, so a TA call can never hang on us.
 */
static DECLARE_WAIT_QUEUE_HEAD(lst_wq);
static DECLARE_COMPLETION(lst_done);
static DEFINE_MUTEX(lst_dev_lock);
static struct { u32 id; void *buf; size_t size; bool pending, taken; int status; } lst_req;
static atomic_t lst_daemon = ATOMIC_INIT(0);

static size_t listener_size(u32 id);

static int listener_serve(u32 id, void *priv)
{
	unsigned long left;

	if (!atomic_read(&lst_daemon)) {
		pr_info("duo_qsee: listener %#x called (no daemon): %*ph\n", id, 32, priv);
		return -EIO;
	}
	mutex_lock(&lst_dev_lock);
	reinit_completion(&lst_done);
	lst_req.id = id;
	lst_req.buf = priv;
	lst_req.size = listener_size(id);
	lst_req.status = -EIO;
	lst_req.taken = false;
	lst_req.pending = true;
	mutex_unlock(&lst_dev_lock);
	wake_up_interruptible(&lst_wq);
	left = wait_for_completion_timeout(&lst_done, 10 * HZ);
	mutex_lock(&lst_dev_lock);
	lst_req.pending = false;
	mutex_unlock(&lst_dev_lock);
	if (!left) {
		pr_warn("duo_qsee: listener %#x: no answer from the daemon in 10 s\n", id);
		return -ETIMEDOUT;
	}
	return lst_req.status;
}

static int lst_open(struct inode *i, struct file *f)
{
	if (atomic_cmpxchg(&lst_daemon, 0, 1))
		return -EBUSY;	/* one daemon serves every listener (one queue) */
	return 0;
}

static int lst_release(struct inode *i, struct file *f)
{
	atomic_set(&lst_daemon, 0);
	return 0;
}

static ssize_t lst_read(struct file *f, char __user *ubuf, size_t n, loff_t *pos)
{
	u32 hdr[2];
	int ret;

	ret = wait_event_interruptible(lst_wq, lst_req.pending && !lst_req.taken);
	if (ret)
		return ret;
	mutex_lock(&lst_dev_lock);
	if (!lst_req.pending || lst_req.taken || n < 8 + lst_req.size) {
		mutex_unlock(&lst_dev_lock);
		return -EINVAL;
	}
	hdr[0] = lst_req.id;
	hdr[1] = lst_req.size;
	if (copy_to_user(ubuf, hdr, 8) || copy_to_user(ubuf + 8, lst_req.buf, lst_req.size)) {
		mutex_unlock(&lst_dev_lock);
		return -EFAULT;
	}
	lst_req.taken = true;
	mutex_unlock(&lst_dev_lock);
	return 8 + lst_req.size;
}

static ssize_t lst_write(struct file *f, const char __user *ubuf, size_t n, loff_t *pos)
{
	s32 hdr[2];

	if (n < 8 || copy_from_user(hdr, ubuf, 8))
		return -EFAULT;
	mutex_lock(&lst_dev_lock);
	if (!lst_req.pending || !lst_req.taken || (u32)hdr[0] != lst_req.id || n - 8 > lst_req.size) {
		mutex_unlock(&lst_dev_lock);
		return -EINVAL;
	}
	if (copy_from_user(lst_req.buf, ubuf + 8, n - 8)) {
		mutex_unlock(&lst_dev_lock);
		return -EFAULT;
	}
	lst_req.status = hdr[1];
	mutex_unlock(&lst_dev_lock);
	complete(&lst_done);
	return n;
}

static __poll_t lst_poll(struct file *f, poll_table *wait)
{
	poll_wait(f, &lst_wq, wait);
	return (lst_req.pending && !lst_req.taken) ? EPOLLIN | EPOLLRDNORM : 0;
}

static const struct file_operations lst_fops = {
	.owner = THIS_MODULE, .open = lst_open, .release = lst_release, .read = lst_read, .write = lst_write,
	.poll = lst_poll, .llseek = noop_llseek,
};

static struct miscdevice lst_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "duo_qsee_listener", .fops = &lst_fops, .mode = 0600,
};
static bool lst_misc_ok;

static void listeners_setup(void)
{
	struct qcom_tzmem_pool_config cfg = { .policy = QCOM_TZMEM_POLICY_STATIC };
	int i, ret;

	if (!nr_listeners)
		return;
	lst_misc_ok = !misc_register(&lst_misc);
	for (i = 0; i < nr_listeners; i++)
		cfg.initial_size += PAGE_ALIGN(listener_size(listeners[i]));
	lst_pool = devm_qcom_tzmem_pool_new(&pdev->dev, &cfg);
	if (IS_ERR(lst_pool)) {
		pr_err("duo_qsee: listener pool: %ld\n", PTR_ERR(lst_pool));
		lst_pool = NULL;
		return;
	}
	for (i = 0; i < nr_listeners; i++) {
		size_t sz = listener_size(listeners[i]);

		lst_buf[i] = qcom_tzmem_alloc(lst_pool, sz, GFP_KERNEL);
		if (!lst_buf[i])
			continue;
		memset(lst_buf[i], 0, sz);
		ret = qcom_scm_qseecom_listener_register(listeners[i], qcom_tzmem_to_phys(lst_buf[i]), sz,
							 listener_serve, lst_buf[i]);
		lst_ok[i] = !ret;
		pr_info("duo_qsee: listener %#x (%zu bytes): register %d\n", listeners[i], sz, ret);
	}
}

static void listeners_teardown(void)
{
	int i;

	for (i = 0; i < nr_listeners; i++) {
		if (lst_ok[i])
			pr_info("duo_qsee: listener %#x unregister %d\n", listeners[i],
				qcom_scm_qseecom_listener_unregister(listeners[i]));
		if (lst_buf[i])
			qcom_tzmem_free(lst_buf[i]);
	}
	if (lst_misc_ok)
		misc_deregister(&lst_misc);
}

static int __init duo_qsee_init(void)
{
	size_t mdt_len;
	ssize_t len;
	void *img;
	int i, ret;

	if (!qcom_scm_is_available())
		return -ENODEV;
	pdev = platform_device_register_simple("duo_qsee", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	listeners_setup();
	pr_info("duo_qsee: asking the secure world which trusted apps are loaded\n");
	for (i = 0; i < ARRAY_SIZE(known); i++)
		lookup(known[i]);
	for (i = 0; i < nr_names; i++)
		lookup(names[i]);

	for (i = 0; i < nr_services; i++) {
		len = read_image(services[i], &img, &mdt_len);
		if (len < 0)
			continue;
		ret = qcom_scm_qseecom_services_load(img, mdt_len, len);
		vfree(img);
		pr_info("duo_qsee: services image %s: %s (%d)\n", services[i], ret ? "FAILED" : "loaded", ret);
	}
	if (app) {
		len = read_image(app, &img, &mdt_len);
		if (len >= 0) {
			ret = qcom_scm_qseecom_app_start(img, mdt_len, len, &started_app);
			vfree(img);
			if (ret)
				pr_info("duo_qsee: app %s: start FAILED (%d)\n", app, ret);
			else
				pr_info("duo_qsee: app %s: STARTED, app id %u\n", app, started_app);
			if (!ret && fpc_info)
				fpc_build_info(started_app);
			if (!ret)
				debugfs_setup();
		}
	}
	pr_info("duo_qsee: done\n");
	return 0;
}

static void __exit duo_qsee_exit(void)
{
	debugfs_remove_recursive(dbg_dir);
	if (raw_req)
		qcom_tzmem_free(raw_req);
	if (raw_rsp)
		qcom_tzmem_free(raw_rsp);
	if (dbg_sh)
		qcom_tzmem_free(dbg_sh);
	if (dbg_rsp)
		qcom_tzmem_free(dbg_rsp);
	if (dbg_req)
		qcom_tzmem_free(dbg_req);
	if (started_app)
		pr_info("duo_qsee: app id %u shutdown: %d\n", started_app, qcom_scm_qseecom_app_shutdown(started_app));
	listeners_teardown();
	platform_device_unregister(pdev);
}

module_init(duo_qsee_init);
module_exit(duo_qsee_exit);
MODULE_DESCRIPTION("Surface Duo secure-world (QSEE) trusted-application probe and loader");
MODULE_LICENSE("GPL");
