# 2048 Application Store Publish Record

Portal: <https://dev.cardputer.cc/#/upload>

## Package

- Debian package path: `artifacts/publish-20261125/2048_0.1.0-m5stack1_arm64.deb`
- Package: `2048`
- Version: `0.1.0-m5stack1`
- Architecture: `arm64`
- Maintainer: `Zhu Zhe <1917345+zhuzhe1983@users.noreply.github.com>`
- APPLaunch desktop path: `/usr/share/APPLaunch/applications/2048.desktop`
- Executable path: `/usr/share/APPLaunch/bin/M5CardputerZero-2048`
- Package SHA-256: `0f149404f483c6789fa0b156947a9cc0493d6f2d534475e247c5177e49945906`
- Executable SHA-256: `4b8e5ee05920c55bd72cffcb4b8fe717a03690f8978a3715947cf84f22601dc2`

## Source and Ownership

- Public source repository: <https://github.com/zhuzhe1983/cp0-2048>
- Expected GitHub uploader/owner: `zhuzhe1983`
- Hide uploader email: `true`; package control also uses the GitHub noreply identity.

## Listing

- Application name: `2048`
- Summary: Classic 2048 puzzle game tuned for the CardputerZero keyboard.
- Description and localized summaries: see `app-builder.json` → `store`.
- Categories: `Games`
- Author: `Zhu Zhe` / `zhuzhe1983`
- License: `MIT` (existing project declaration)
- Share code: `2048` (server uniqueness check passed)
- The seven current permission declarations are explicitly `false`; keyboard and built-in speaker do not require the optional capabilities offered by the portal.

## Assets

- Source icon (256 x 256): `share/images/2048.png`
- Packaged icon: `/usr/share/APPLaunch/share/images/2048.png`
- Screenshot 1: `store/screenshots/2048-start-320x170.png`
- Screenshot 2: `store/screenshots/2048-midgame-320x170.png`
- Screenshot 3: `store/screenshots/2048-win-320x170.png`
- Screenshot 4: `store/screenshots/2048-gameover-320x170.png`
- All screenshots visually checked: clean 320 x 170 PNG framebuffer content.

## Preflight

- [x] `.deb` exists and can be parsed.
- [x] Required package fields, APPLaunch desktop, ARM64 executable and square PNG are present.
- [x] Filename version and control version match; archive ownership is root/root.
- [x] Runtime shared-library dependencies declared.
- [x] Local strict prepublish check passes.
- [x] Portal recognized the initial package as a new package name for `zhuzhe1983`.
- [x] Listing categories, author, share code and seven boolean permissions prepared to current rules.
- [x] Four clean 320 x 170 screenshots checked.
- [x] User explicitly requested external submission and supplied the signed-in browser.
- [x] Updated package passes portal preflight; server accepted the web submission.
- [x] Real-device installation, ARM64 binary checksum, runtime libraries, framebuffer launch, keyboard movement, restart prompt/cancel and long-Esc exit verified; APPLaunch restored active.
- [x] User-recorded physical-device video attached to the resulting PR and visually checked for launch, gameplay and exit.

## Device Installation

- Status: Installed and tested as the non-root `pi` user. APPLaunch is active and ready for the user to record the physical demonstration.
- Backup path: `/home/pi/2048-backups/20261125-201349` (no previous 2048 package or app files were installed).
- Verification captures: `artifacts/publish-20261125/device-start.png`, `device-moved.png`, `device-restart.png`. These are framebuffer QA captures, not the required physical-device review video.
- Do not include SSH credentials or private infrastructure details in the public repository.

## Submission Result

- Status: `publish PR created; automated checks passed; real-device video attached and checked; awaiting maintainer review`
- Submission observed at: 2026-11-25T20:21:50+08:00
- Portal message: Submitted; server is performing final validation and will create a publish PR after it passes.
- Successful validation workflow: <https://github.com/CardputerZero/packages/actions/runs/45614052011>
- Publish PR: <https://github.com/CardputerZero/packages/pull/819>
- Review requirement confirmed by the store bot: <https://github.com/CardputerZero/packages/pull/819#issuecomment-7162422919>
- PR created: `2026-11-25T12:28:30Z`; PR head: `20fbcf258d06a162b13790934e71fb0b3014505e`.
- Submitted icon: `2048_icon.png`; the portal has a separate uploaded-icon preview.
- Submitted screenshots: three clean real-device captures, `screenshots/screenshot-0.png` (restart prompt), `screenshots/screenshot-1.png` (gameplay), `screenshots/screenshot-2.png` (initial board). The four source screenshots above remain available for subsequent listing updates.
- Generated metadata timestamps: `published_at=2026-11-25T12:28:27+00:00`, `updated_at=2026-11-25T12:28:27+00:00`. These are registry metadata values, not evidence that review or deployment completed.
- First attempt: <https://github.com/CardputerZero/packages/issues/818> reported missing store icon. The package icon passed local/browser checks, but did not populate the separate store icon. Corrected before the successful submission.
- Stage verification: successful workflow executed `Create publish PR`; `Report failure to the submitter` was skipped. PR remains open and unmerged.
- Physical-device video comment: <https://github.com/CardputerZero/packages/pull/819#issuecomment-7162522898>
- Original video: <https://github.com/user-attachments/assets/afd84945-5431-48e1-8b44-be0553a28422>
- Local original: `artifacts/publish-20261125/physical-demo-original.mp4` (15.033 seconds, 1280 x 720 HEVC video, 48 kHz AAC audio). Original retained intact.
- Visual verification: launcher and 2048 selection at the start; app launch by approximately 3 seconds; keyboard moves and score increases during gameplay; long Esc around 13 seconds; launcher restored by approximately 14 seconds. Fifteen timeline frames were inspected across the full recording.
- Final remaining step: store maintainer review and merge, followed by catalog deployment. The PR was still open and unmerged when checked.
- New applications require maintainer review; upload, automated-check success and video attachment do not by themselves establish live store availability.

## Skill Maintenance

- Updated the installed development/publishing skills (symlinked to `../skill/`).
- Documented current metadata rules, explicit store-icon upload, form reset behavior, asynchronous failure-report handling and required real-device review video.
- Updated preflight validation for current metadata, PNG dimensions, version/filename consistency and ARM64 ELF checks.
- Corrected package-helper revision consistency, root archive ownership and explicit runtime-dependency support.
- Validation: both skills pass `quick_validate.py`; real 2048 package passes strict preflight; legacy metadata and five invalid listing cases were rejected; both package-version forms produced correct archives.
- The skill repository already contained unrelated uncommitted typography/launcher-contract changes. They were preserved, and the skill work remains uncommitted for review.
