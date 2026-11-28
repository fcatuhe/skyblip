# server/test/tla

A TLA+ specification of the briefing app's firmware page, checked with TLC. It models the code as it runs, not a design, so a change to `app/javascript/controllers/firmware_controller.js`, `controllers/briefing/shelf.js` or the install half of `skyblip/update.js` changes it too. CI does not run it, because TLC needs Java.

| Spec | What it models | What TLC checks |
|---|---|---|
| `FirmwarePage.tla` | one unit taking image after image without a reload: the shelf and the file input, Install, the question on the glass, the upload, the verdict, the restart into the image or back on the old one, and the pilot leaving the page and coming back | an image the pilot picked is offered as soon as the unit can take it, the file input takes the next build of the same name, the page says done only for a unit that restarted after Install on the version asked, and a success or a return to the old version is said even to a page that was away during the restart |

`FirmwarePageAsWas.cfg` runs it with `Fixed = FALSE`, the code before the Updater kept the install it started. TLC finds four gaps, one per invariant when each is checked alone:

- `PickIsInstallable`, in eight states: Install for an image of the version the unit runs, a question nobody answers, then a newer image picked from the shelf. The page kept the version of the last Install on the `Unit` and counted it done whenever the unit ran that version, so Install stayed hidden for every image picked after it, and only a reload brought it back. A success is the common way in. The shortest trace, in six states, picks an image of the running version, leaves for the device page and comes back: the new controller forgot the pick and hid Install as for an image nobody chose.
- `NextBuildLoadable`, in six states: a file chosen, Install, a question nobody answers. The file input still held `skyblip-go.signed.bin`, and a file input fires no change for the name it holds, so the next build of the same name never reached the page.
- `DoneMeansInstalled`, in five states: Install for an image of the version the unit runs, such as its full image. The page said `Running` that version, done, while the glass still asked, and kept saying it after a refusal or a failed upload.
- `FailureIsSaid`, in eleven states: an install that lands, the pilot on the device page while the unit restarts back on its old version, then the firmware page again. Only an open page marked the restart, so the page that came back never said the install failed.

The code now keeps the install in `Updater#state.attempt`, the version asked and whether the unit restarted since, which a new image ends. The firmware page counts an install done only once the unit restarted and runs that version, `shelf.js` keeps the pilot's pick per unit, and Install empties the file input.

Run from this folder, with the jar from [github.com/tlaplus/tlaplus/releases](https://github.com/tlaplus/tlaplus/releases):

```
java -cp tla2tools.jar tlc2.TLC -workers auto -config FirmwarePage.cfg FirmwarePage.tla
java -cp tla2tools.jar tlc2.TLC -workers auto -config FirmwarePageAsWas.cfg FirmwarePage.tla
```

On 28 November 2026, with TLC 2026.11.26, `FirmwarePage` explored 58,020 distinct states in two seconds on eight cores with no error.
