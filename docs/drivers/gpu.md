# Workbench — GPU (Adreno 640)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

## Log
- 2026-11-08 20:10 **0090 v1 broke the boot; v2 keeps fixed ranges at page alignment.** v1 aligned EVERY object of
  64 KiB+ to 64 KiB, but the GMU allocates some buffers at fixed addresses with a range exactly their size (the icache:
  ~16 MB at iova 0x4000 - not 64 KiB aligned) -> no room -> GMU init fails; the phone reset back to fastboot ~30 s into
  every 00090 boot (UEFI one-shot and ABL alike; the "early hangs" of the evening). v2: try 64 KiB, fall back to
  PAGE_SIZE on -ENOSPC. **RESULT (20:00-20:10): FIXED.** 6.19.0-00090-g06fb27fd0b7e via ABL -> Debian: duo-health 0
  FAIL (after its module set), **0 GPU faults / 0 hangchecks** over boot + 3 plasmashell restarts (before: a CCU fault
  ~16 s after EVERY plasmashell start). Promoted as the default (ESP vmlinuz.efi dd104669, initramfs b57b8b2f; .prev =
  00089 3e26480e/061607f2); plain UEFI reboot -> workbench on 00090, 0 faults. The patch is an upstream candidate
  (docs/upstream): the CCU reading flag data from the 64 KiB-aligned block start is an Adreno a6xx property.
- 2026-11-08 23:20 **crashdec says: the CCU reads BELOW a UBWC render target.** Mesa 26.1.6 freedreno tools built on
  the phone (`tools/linux/desktop/duo-fdtools-build.sh`; run `crashdec -m -f <dump>` - without -f it reads stdin and
  segfaults). The faulting submit = plasmashell drawing a 1440x176 panel surface (RGBA8, TILE6_3, UBWC, 2 bins of
  736x176): flag buffer 0x1066da000 (= the BO start), color 0x1066dc000, depth+flags in the next BO. Nothing in the
  submit references 0x1066d0000 - but **0x1066da000 rounded down to 64 KiB = 0x1066d0000**, the fault address. A second
  boot faulted 9 times, always at another 64 KiB-aligned iova (0x106590000). So the hardware fetches flag data in
  64 KiB-aligned blocks and our kernel places BOs only 4 KiB-aligned (kernel-managed VMs: `drm_mm_insert_node_in_range(...,
  PAGE_SIZE ...)`); kgsl aligns every allocation >= 64 KiB to 64 KiB. The earlier "use-after-free" idea is dropped
  (the valid PTE at dump time = a neighbour mapped later into the gap). Test: series 0090 (64 KiB alignment for
  objects >= 64 KiB).
- 2026-11-08 18:00 **the boot hang, narrowed (kernel 00089, Mesa 26.1.6).** Repro: switching to Plasma (plasmashell
  start) -> 16 s later `gpu fault iova=0x1066d0000 READ TRANSLATION CCU` + hangcheck recover, offending
  plasmashell's GL thread - every time. One-shot boot with `msm.vm_log_shift=8`: no VM log in the devcoredump because
  the log exists only for userspace-managed (VM_BIND) VMs - **plasmashell's VM is kernel-managed** (gallium freedreno
  26.1.6 has no VM_BIND path; its only env switches are FD_MESA_DEBUG / FD_MESA_GMEM / FD_RD_DUMP*). The dump's
  pgtable walk again shows a VALID last-level PTE (00000001a045be43) for the faulting iova, and no BO of the submit
  covers it (nearest starts 0x1066da000). Kernel-managed VMs map at submit time, so "mapped too late" is unlikely;
  what fits is a **GPU use-after-free**: a buffer the GPU still reads was unmapped (not in / not kept alive by the
  submit), the GPU faulted, and the kernel reused the iova for a new mapping before the dump. Page-table coherency
  ruled out (adreno_smmu not dma-coherent -> io-pgtable cleans tables + syncs PTEs, also with msm's custom
  allocator). Next: Mesa's freedreno tools (crashdec on the devcoredump, cffdump on an msm_rd capture with
  msm.rd_full=1) to name the draw/blit that reads the address and the buffer it was - script
  `tools/linux/desktop/duo-fdtools-build.sh` (outside code: the tester runs it). Devcoredump kept in a local folder
  (gpu-devcd.txt, 2.8 MB).
- 2026-11-08 17:30-18:10 **RESULT: the 13 backports fix the faults under load.** `-gpufix-00086` per-scene run
  (Mesa 26.1.6): **0 faults, 0 hangs over all 13 scenes** (stock 6.19: 23-24 faults + 1-2 hangs). Promoted into the
  series as 0074-0086 (12 lazy-VM patches from v7.3 + b303e1d52811 hand-ported). Still open: ONE GPU hang ~37 s after
  every boot in plasmashell's GL thread (CCU READ translation fault, then recovery in ~0.1 s; a brief glitch at
  boot, nothing freezes). Ruled out: Mesa 25.0.7 vs 26.1.6, the kernel fixes above, `-gpufix-00087` = an a6xx fence
  wait before CP_SMMU_TABLE_UPDATE (CP_WAIT_REG_MEM on the ring fence; same hang, kept as tag
  gpufix-a6xx-fencewait-tried in the WSL tree), and FD_MESA_DEBUG=nobypass (set in kwin + plasmashell, same hang ->
  not freedreno's sysmem/bypass path). Next idea: decode the devcoredump's IBs with Mesa's crashdec (which draw /
  blit), and check whether it is a Plasma-specific effect (e.g. blur/contrast behind the panels).
- 2026-11-08 17:10-17:30 **test kernels.** `-gpufix-00086` (12 lazy-VM patches + b303e1d52811 hand-ported) boots
  Debian, duo-health 0 FAIL; boot: the plasmashell hang at 38 s is STILL there but only 2 faults (was 16 - the
  recover-before-retire fix stops the burst after a hang); per-scene run (Mesa 26.1.6): 0 faults in the first 6
  scenes (baseline 1-4 each). Found in mainline history: 77ad507dbb7e "drm/msm/a6xx+: Insert a fence wait before
  SMMU table update" v2 limited the wait to a7xx ("CP_WAIT_TIMESTAMP ... not supported on a6xx ... I've not been able
  to reproduce this issue on a6xx") - our A640 has no wait before CP_SMMU_TABLE_UPDATE at all. `-gpufix-00087` adds
  ours: "drm/msm/a6xx: wait for the previous submit before a pagetable switch" (CP_WAIT_REG_MEM polling the ring
  fence in memory, WRITE_GE seqno-1) - aimed at the boot hang. Upstream candidate if it holds.
- 2026-11-08 16:25-16:45 Mesa 26.1.6 per-scene run: 24 faults + 1 hang over 13 scenes (25.0.7: 23 + 2) = same rate,
  faster (shadow 525 vs 333 fps, effect2d 858 vs 387). All faults of that boot: ONE iova (0x1182b0000) in FIVE
  different page tables (17 of 24 in plasmashell's) -> the same buffer slot in each process (shared/imported
  buffers?), the shell redrawing as benchmark windows open/close - not the benchmark's own rendering. Mainline
  (v7.4-rc1+) has the lazy-VM series (v7.3) + "drm/msm: Recover HW before retire hung submit" (b303e1d52811,
  v7.3: retiring the hung submit before recovery frees BOs the GPU still reads -> faults after every hang). Test
  branch duo/gpufix-test (WSL /root/linux-gpufix): 12 of the 17 lazy-VM patches apply to 6.19 (the 5 msm_gem.c
  import patches need other post-6.19 changes), b303e1d52811 hand-ported (same 2-line swap inside our
  msm_gpu_active() block). msm is built in (CONFIG_DRM_MSM=y) -> one-shot test kernel, LOCALVERSION -gpufix.
- 2026-11-08 16:00-16:25 **GPU fault diagnosis.** (1) Per scene (duo-gpu-scenes.sh, 45 s each, Mesa 25.0.7): 23 faults
  over 13 scenes, ~2 per scene whatever the load (texture/bump/desktop 0; build + buffer also 1 hang each) -> not a
  heavy-shader bug; faults come with buffer/context churn. (2) Mesa 26.1.6 (trixie-backports, kept: ~2x fps in light
  scenes - build 1206 vs 547, texture 1090 vs 785) STILL faults (5 in the first 2 scenes) and the boot again hung
  plasmashell's GL thread at 38 s -> NOT Mesa. (3) The pmOS kmsg logs on sda11 (0 faults) are inconclusive: they
  cover only 65 / 153 s and the GPU firmware never loaded in them. (4) devcoredump of the 38 s plasmashell hang:
  fault iova 0x117a50000 (reported 64 KiB-aligned), a live 1 MiB BO starts at 0x117a53000 inside that window, and
  the pgtable walk at dump time shows a VALID last-level PTE (...2e43) -> the GPU took a TRANSLATION fault on an
  address that IS mapped = a TLB / map-ordering race in msm's per-process page tables, not a freed buffer.
  Firmware (sqe, GMU v2.0.261, zap from vendor_a) and the SM8150 UBWC table (UBWC 3.0, hbb 15 = downstream) checked
  fine. Candidate fix upstream: Rob Clark's "drm/msm: A couple lazy-vm fixes" v7 (2026-07, 17 patches: "Fix barriers
  accessing ctx vm", "Synchronize VM creation on ctxlock", "a6xx: Access VM directly in submit path" ...) - races in
  the lazy per-context VM creation that 6.17+ (VM_BIND) introduced; our faults cluster at context start. Next:
  backport onto duo/v6.19-clean and A/B with duo-gpu-scenes.sh.
- 2026-11-08 15:49-15:59 (Debian, kernel 6.19.0-00073, Mesa 25.0.7 freedreno "FD640", the tester: "We need to confirm the
  GPU works under heavy load"): **10 min of glmark2's heaviest scenes** (terrain, refract, jellyfish, shadow, ideas,
  blur; `tools/linux/desktop/duo-gpu-stress.sh gl 600`, panels kept on with kde-inhibit - the first try ran with
  Plasma's idle DPMS off, the compositor withholds frames then, invalid). Thermals FINE: GPU pinned at 585 MHz (the
  top bin) the whole time, GPU 38-58 C, skin 38 C max, CPU 54 C, batteries 24 / 35 C (charging), no throttling; scores
  terrain 74 fps, refract 59, jellyfish 387, shadow 333, ideas 703 (1587x2118 fullscreen). **BUT not stable:** 84 GPU
  page faults (`gpu fault ... dir=READ type=TRANSLATION source=CCU`, in bursts of ~10 + "callbacks suppressed", 12
  distinct iovas) and 3 hangcheck recoveries in the boot - plasmashell 37 s after boot (before any test), glmark2,
  kwin_wayland. The phone never froze (recovery works) but desktop use can glitch. CCU = the color cache (blits,
  sysmem render targets); a READ translation fault = the GPU reads through a mapping that is gone (buffer freed or
  never mapped). Next: which scene faults (duo-gpu-scenes.sh), then Mesa 26.1.6 from trixie-backports A/B.
- 2026-11-08 01:10: **Newer Mesa does NOT fix the CCU fault.** Mesa 26.2.6 Turnip (Alpine edge apk, unpacked in /tmp
  with edge libdisplay-info + wayland-libs-client via LD_LIBRARY_PATH + VK_ICD_FILENAMES - nothing installed) runs on the
  Adreno 640 (API 1.3.354). vkcube A/B, 200 frames each: 25.1.9 = 0 then 2 faults; 26.2.6 = SIGSEGV + 3 faults + 1
  hangcheck (recovered), then 2 faults. Same iova 0x1008f9d00 (CCU WRITE PERMISSION) every time -> a deterministic
  Turnip write into its own read-only BO on the VM_BIND msm kernel, not fixed upstream. Rendering still fine.
  Next: FD_RD_DUMP capture + crashdec to name the packet, then a Mesa issue (upstream candidates #12, draft).
- 2026-11-07 night: **VULKAN WORKS on mainline** (pmOS, kernel 00067): `vulkaninfo --summary` = Turnip Adreno 640,
  Mesa 25.1.9, API 1.3.311, conformance 1.2.7.1. `vkcube --wsi wayland --c 200` renders 200 frames in ~3.4 s (~59 fps,
  the panel rate) - same Mesa driver matrix-duo uses on KGSL, here over mainline msm. BUT every run logs 2 GPU faults
  `dir=WRITE type=PERMISSION source=CCU` at nearly the same iova (0x1008f7d00 / 0x1008f9b00), with and without
  TU_DEBUG=sysmem / noubwc (so not GMEM tiling or UBWC); the first run hung once ("hangcheck recover", offending task
  vkcube). Same fault class as the WebKit DMA-BUF runaway (2026-11-06) -> one GPU-stack bug: something in the Wayland
  swapchain path writes into a buffer the GPU maps read-only. Next: identify the faulting BO (msm debugfs / MSM_BO
  flags of imported dma-bufs), compare with GL (kmscube/es2gears on Wayland).
  **Faulting BO found** (msm debugfs `dri/1/gem` while vkcube runs): fault iova 0x1008f9d00 lies in a vkcube BO at
  0x1008f9000, 16 KiB, flags 0x00020002 = MSM_BO_WC | **MSM_BO_GPU_READONLY**. Turnip makes its command-stream BOs
  GPU read-only on purpose (mesa MR 10807); the CCU then writes into one -> on our 6.19 kernel (VM_BIND-era msm) the
  read-only mapping is enforced -> permission fault. Not found as a known issue (searched 2026-11-07). Leads: which
  Turnip path writes via CCU into a cmdstream/suballoc BO (blit/clear/query target placed in a read-only suballoc?),
  and whether newer Mesa (25.2+) changed it - test a newer Mesa build (outside code -> the tester builds) before any
  kernel change. Rendering itself is fine (~59 fps), so this is a correctness item, not a blocker.

- 2026-11-04 11:25: **GPU-RENDERED CUBE ON THE PANEL** (kmscube on DSI-1 = the right panel as the camera sees it; photo in (local file, not published)). The "flips never reach the panel" note below was WRONG: kmscube quits on stdin EOF ("user interrupted!") — with </dev/null it exited at once, and over ssh without nohup it died with the session. Run it as `nohup sh -c "sleep 600 | kmscube -D /dev/dri/card1"`. DRM debug showed real cmd-mode kickoffs on pp:0. Open: frame pacing (no vsync wait: -c 20000 finished in <2 s) and the second panel (kmscube drives one connector).
- 2026-11-04 11:15: **GPU RENDERS:** Mesa 25.1.9 freedreno via a gfx overlay (`OVERLAY_BASE="mesa-dri-gallium mesa-egl mesa-gbm mesa-gles kmscube mesa-utils" make-dev-overlay.sh`, 89 MB, RAM) → kmscube: "OpenGL ES 3.2 Mesa 25.1.9, vendor freedreno, renderer FD640", no errors. BUT the BRIO shows **only the right panel lit**: drmfill bars appear on one panel, the other stays dark (setcrtc=0 on both connectors, panel params default, no DSI errors) — the left panel was dark already at 10:28/10:37 (before charger/GPU) → a display regression since kernel #48's 6/6; the kmscube cube went to the dark panel. Next: DSI1 MISR check (leg 34), bisect kernel #48 vs #56.
- 2026-11-04 11:06: **GPU WORKS (initializes) + the test-boot hangs SOLVED:** GRUB's TEST entry 10 lacked `msm.dsi_cmd_burst=0` (the display fix), so every one-shot test boot ran the display unfixed → the 10:16 and 11:00 early hard hangs. Entry 10 now carries it before consoleblank=0 (kept by duo-grub-test.sh). GPU one-shot then booted; opening /dev/dri/renderD128 loaded a630_sqe.fw, a640_gmu.bin (GMU fw v2.0.261) and the a640_zap.mdt zap shader → debugfs gpu: initialized, revision 640, RBBM idle. DTB #51 = default with &gpu/&gmu: normal boot verified (GPU + charging + both panels + Wi-Fi).
- 2026-11-04 11:00: GPU one-shot DT test (DTB #50-gpu: &gpu/&gmu okay; init #62 links a630_sqe/a640_gmu/a640_zap from vendor_a) HARD-HUNG before the panels lit (no USB, no watchdog) → the tester's power button. Pattern to check: both hung test boots today (charger 10:16, GPU 11:00) went dark before the display; the 09:07/09:08 DTB= Wi-Fi tests were fine. GPU stays out of the default DT.
- 2026-11-04 10:10: the tester: finish Linux before Windows → the work plan reordered (GPU, haptics, portrait UI + OSK, UFS gears, mics, CDSP client, modem/GPS, suspend); driver matrix refreshed.
