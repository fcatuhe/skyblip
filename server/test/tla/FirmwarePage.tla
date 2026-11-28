---------------------------- MODULE FirmwarePage ----------------------------
(***************************************************************************)
(* The briefing app's firmware page installing one image after another on *)
(* one unit, without a reload: firmware_controller.js drawing Install and *)
(* the success, shelf.js taking an image, update.js asking, uploading and  *)
(* waiting for the unit to come back, and the unit restarting into what    *)
(* landed, or not.                                                         *)
(*                                                                         *)
(* A development unit takes any of three images: a base one of version 1, *)
(* and a base and a full one of version 2, which share their version. The *)
(* file input holds one name, skyblip-go.signed.bin, whose content a       *)
(* rebuild changes. The pilot can leave the page for the device page and   *)
(* come back while the unit installs.                                      *)
(*                                                                         *)
(* Fixed = FALSE models the code as it stood: the page kept the attempt on *)
(* the Unit, saw the restart only while it was open, and kept the pick in  *)
(* the controller. Fixed = TRUE models the code as changed.                *)
(***************************************************************************)
EXTENDS Naturals

CONSTANT Fixed

None == "none"
NoVersion == 0
Images == {"b1", "b2", "f2"}
Version(i) == IF i = "b1" THEN 1 ELSE 2
Newest == "b2"

Phases == {"offline", "ready", "asking", "confirming", "uploading", "installing", "rebooting"}
Working == {"asking", "confirming", "uploading", "installing", "rebooting"}
Linked == {"ready", "asking", "confirming", "uploading", "installing"}

VARIABLE s

Init ==
    \E d \in Images :
        s = [running |-> "b1", image |-> "confirmed", staged |-> None, flight |-> "ground",
             phase |-> "offline", task |-> None, file |-> None, refused |-> FALSE,
             attempted |-> NoVersion, restarted |-> FALSE, offered |-> FALSE,
             open |-> TRUE, picked |-> FALSE, taking |-> None, takingDisk |-> FALSE,
             shelf |-> None, fileFromShelf |-> FALSE, inputHeld |-> FALSE, disk |-> d,
             clicked |-> None, restartedSinceClick |-> FALSE, pickedSinceClick |-> FALSE]

(* What the page draws, as #render computes it *)
Idle == s.phase = "ready" /\ s.flight = "ground"
Asks == Idle /\ ~s.refused
Settling == s.phase = "ready" /\ s.image = "probation"
Busy == s.phase \in Working \/ s.image = "probation"
Done == s.attempted # NoVersion /\ (Fixed => s.restarted)
        /\ Version(s.running) = s.attempted /\ s.image = "confirmed"
Upgrade == IF s.file = None THEN None
           ELSE IF Version(s.file) > Version(s.running) THEN "true" ELSE "false"
InstallShown == ~(s.refused \/ (s.phase \in Working /\ s.phase # "asking") \/ Settling \/ Done
                  \/ (Upgrade = "false" /\ ~s.picked))
InstallEnabled == Asks /\ s.file # None /\ s.taking = None
Offered == s.open /\ InstallShown /\ InstallEnabled
CanPick == s.open /\ Idle /\ s.taking = None /\ ~Busy
NoRecord == s.restarted /\ s.attempted # NoVersion /\ Version(s.running) # s.attempted
            /\ s.image = "confirmed"

(* As it stood, #render set Unit.restarted when it drew a restart, so only an open page saw one *)
Step(t) == s' = IF ~Fixed /\ t.open /\ t.phase = "rebooting" THEN [t EXCEPT !.restarted = TRUE] ELSE t

(* shelf.js take() and Updater#clearImage: a new image ends the attempt *)
Taking(t, i, disk) ==
    [t EXCEPT !.taking = i, !.takingDisk = disk, !.file = None, !.fileFromShelf = FALSE,
              !.attempted = IF Fixed THEN NoVersion ELSE t.attempted,
              !.restarted = IF Fixed THEN FALSE ELSE t.restarted]

Pick(i) ==
    /\ CanPick /\ i # s.shelf
    /\ Step([Taking(s, i, FALSE) EXCEPT !.picked = TRUE, !.inputHeld = FALSE, !.shelf = i,
                                        !.pickedSinceClick = TRUE])

(* A file input fires no change for the name it holds already *)
ChooseFile ==
    /\ CanPick /\ ~s.inputHeld
    /\ Step([Taking(s, s.disk, TRUE) EXCEPT !.picked = TRUE, !.inputHeld = TRUE, !.shelf = None,
                                            !.pickedSinceClick = TRUE])

Rebuild == \E d \in Images \ {s.disk} : Step([s EXCEPT !.disk = d])

Fetched ==
    /\ s.taking # None
    /\ Step([s EXCEPT !.file = s.taking, !.fileFromShelf = ~s.takingDisk, !.taking = None,
                      !.takingDisk = FALSE])

(* offerNewest: once a connection, the newest base image, which the pilot did not pick *)
OfferNewest ==
    /\ s.phase \in Linked /\ s.file = None /\ s.taking = None /\ ~s.offered
    /\ Step([Taking(s, Newest, FALSE) EXCEPT !.offered = TRUE,
                                             !.picked = IF Fixed THEN FALSE ELSE s.picked])

(* firmware#install, then Updater#install asking dfu *)
ClickInstall ==
    /\ Offered
    /\ Step([s EXCEPT !.attempted = Version(s.file), !.restarted = FALSE,
                      !.phase = "asking", !.task = "dfu",
                      !.inputHeld = IF Fixed THEN FALSE ELSE s.inputHeld,
                      !.clicked = s.file, !.restartedSinceClick = FALSE, !.pickedSinceClick = FALSE])

Pending == s.phase = "asking" /\ s.task = "dfu" /\ Step([s EXCEPT !.phase = "confirming"])

Grant ==
    /\ s.phase \in {"asking", "confirming"} /\ s.task = "dfu"
    /\ Step([s EXCEPT !.phase = "uploading", !.task = "install"])

RefuseOnGlass ==
    /\ s.phase = "confirming"
    /\ Step([s EXCEPT !.phase = "ready", !.task = None, !.refused = TRUE])

(* A dfu unanswered within a minute; an install verdict cannot be, since the unit restarts at once *)
Unanswered ==
    /\ s.phase \in {"asking", "confirming"} /\ s.task = "dfu"
    /\ Step([s EXCEPT !.phase = "ready", !.task = None])

UploadFails == s.phase = "uploading" /\ Step([s EXCEPT !.phase = "ready", !.task = None])

(* The last chunk lands: the verdict arrives after its reply, or before it *)
Lands ==
    /\ s.phase = "uploading"
    /\ \/ Step([s EXCEPT !.phase = "asking", !.staged = s.file])
       \/ Step([s EXCEPT !.phase = "installing", !.task = None, !.staged = s.file])

Verdict ==
    /\ s.phase = "asking" /\ s.task = "install"
    /\ \/ Step([s EXCEPT !.phase = "installing", !.task = None])
       \/ Step([s EXCEPT !.phase = "ready", !.task = None, !.staged = None])

AwaitingRestart == s.phase = "installing" \/ (s.phase = "asking" /\ s.task = "install")

(* The unit restarts: into the image, back on the old one with no record, or reverted *)
Restart ==
    /\ AwaitingRestart
    /\ \E outcome \in {"swap", "old", "reverted"} :
        Step([s EXCEPT !.phase = "rebooting", !.task = None, !.refused = FALSE,
                       !.running = IF outcome = "swap" THEN s.staged ELSE s.running,
                       !.image = CASE outcome = "swap" -> "probation"
                                   [] outcome = "old" -> "confirmed"
                                   [] OTHER -> "reverted",
                       !.staged = None,
                       !.restarted = IF Fixed /\ s.attempted # NoVersion THEN TRUE ELSE s.restarted,
                       !.restartedSinceClick = TRUE])

Back == s.phase = "rebooting" /\ Step([s EXCEPT !.phase = "ready"])

NotBack == s.phase = "rebooting" /\ Step([s EXCEPT !.phase = "offline"])

Settle == s.image = "probation" /\ Step([s EXCEPT !.image = "confirmed"])

(* The link drops, or the pilot drops it to answer a refusal on the glass *)
Drop ==
    /\ s.phase \in Linked /\ ~AwaitingRestart
    /\ Step([s EXCEPT !.phase = "offline", !.task = None, !.refused = FALSE, !.offered = FALSE])

Connect == s.phase = "offline" /\ Step([s EXCEPT !.phase = "ready"])

Fly == Step([s EXCEPT !.flight = IF s.flight = "ground" THEN "air" ELSE "ground"])

Leave == s.open /\ Step([s EXCEPT !.open = FALSE])

(* A new page: a new controller, an empty file input, and #offer selects the file's image *)
Return ==
    /\ ~s.open
    /\ Step([s EXCEPT !.open = TRUE, !.inputHeld = FALSE,
                      !.picked = IF Fixed THEN s.picked ELSE FALSE,
                      !.shelf = IF s.fileFromShelf THEN s.file ELSE None])

Next ==
    \/ \E i \in Images : Pick(i)
    \/ ChooseFile \/ Rebuild \/ Fetched \/ OfferNewest \/ ClickInstall
    \/ Pending \/ Grant \/ RefuseOnGlass \/ Unanswered \/ UploadFails \/ Lands \/ Verdict
    \/ Restart \/ Back \/ NotBack \/ Settle \/ Drop \/ Connect \/ Fly \/ Leave \/ Return

Spec == Init /\ [][Next]_s

(***************************************************************************)
(* What must hold                                                          *)
(***************************************************************************)
TypeOK ==
    /\ s.phase \in Phases
    /\ s.running \in Images
    /\ s.file \in Images \cup {None}
    /\ s.taking \in Images \cup {None}
    /\ s.attempted \in {NoVersion, 1, 2}

(* An image the pilot picked since the last Install is offered as soon as the unit can take it *)
PickIsInstallable ==
    Asks /\ s.open /\ s.pickedSinceClick /\ s.taking = None /\ ~Settling => Offered

(* Once Install took the file, the file input takes the next build of the same name *)
NextBuildLoadable == CanPick /\ ~s.pickedSinceClick => ~s.inputHeld

(* The page says done only for a unit that restarted after Install and runs the version asked *)
DoneMeansInstalled ==
    Done => s.restartedSinceClick /\ s.clicked # None /\ Version(s.running) = Version(s.clicked)

(* An install that worked says so, even to a page that was away during the restart *)
SuccessIsSaid ==
    s.open /\ s.phase = "ready" /\ s.clicked # None /\ s.restartedSinceClick /\ ~s.pickedSinceClick
    /\ Version(s.running) = Version(s.clicked) /\ s.image = "confirmed"
    => Done

(* A unit back on its old version with no record says so, even to a page that was away *)
FailureIsSaid ==
    s.open /\ s.phase = "ready" /\ s.clicked # None /\ s.restartedSinceClick /\ ~s.pickedSinceClick
    /\ Version(s.running) # Version(s.clicked) /\ s.image = "confirmed"
    => NoRecord

=============================================================================
