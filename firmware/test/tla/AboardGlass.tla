---------------------------- MODULE AboardGlass ----------------------------
(***************************************************************************)
(* The Go's glass deciding what it stores as the aircraft's own: the      *)
(* NewAircraft and NewFlarm notices, the AIRCRAFT and FLARM menus, and     *)
(* the question that erases a FLARM ID, as ScreenService runs them.        *)
(*                                                                         *)
(* One Pass is one ScreenService::handle_input: a gesture obeyed, the      *)
(* editor's timers, sync_notice, then tick's alarm rule. Render is a       *)
(* separate step, because the panel can be busy for several passes, and    *)
(* Settle is the clock passing PromptAnswer::kAnswerSettleMs. The world    *)
(* moves between passes: the flight state, what the detectors hear, an     *)
(* alarm, a phone's prompt.                                                *)
(*                                                                         *)
(* Fixed = FALSE models the code as it stood, with the FLARM flow copied   *)
(* from the transponder's. Fixed = TRUE models the code as changed.        *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS FlarmIds, Aircraft, Fixed

None == "none"
Flights == {"ground", "air", "nofix"}

Seen(k, n, v) == [k |-> k, n |-> n, v |-> v]
Black == Seen("black", None, None)

(* detected() and flarm_detected(): what is heard, or why nothing is *)
DevDetected(s) == IF s.flight # "ground" THEN s.flight
                  ELSE IF s.heardDev = None THEN "listening" ELSE s.heardDev
AcDetected(s) == IF s.flight # "ground" THEN s.flight
                 ELSE IF s.heardAc = None THEN "listening" ELSE s.heardAc

(* detected_to_store(): what the glass showed, while it is still heard *)
ToStore(shown, now, Values) == IF shown \in Values /\ shown = now THEN shown ELSE None

(* What render() draws now *)
Content(s) ==
    IF s.notice # None
    THEN Seen("notice", s.notice,
              CASE s.notice = "dev" -> s.noticedDev
                [] s.notice = "ac" -> s.noticedAc
                [] OTHER -> None)
    ELSE IF s.mode = "menu"
         THEN IF s.clearing THEN Seen("clear", "dev", s.storedDev)
              ELSE IF s.sub = "flarm" THEN Seen("block", "dev", DevDetected(s))
              ELSE IF s.sub = "aircraft" THEN Seen("block", "ac", AcDetected(s))
              ELSE Seen("settings", None, None)
         ELSE Seen("page", None, None)

VARIABLE s

Init ==
    s = [flight |-> "nofix", heardDev |-> None, heardAc |-> None, alarm |-> FALSE,
         storedDev |-> None, storedAc |-> None,
         mode |-> "page", sub |-> "settings", focus |-> "light", clearing |-> FALSE,
         notice |-> None, noticedDev |-> None, noticedAc |-> None,
         devNoticed |-> FALSE, acNoticed |-> FALSE, devNotices |-> 0,
         noticeOnGlass |-> FALSE, noticeSettled |-> FALSE,
         devOnGlass |-> None, acOnGlass |-> None,
         clearOnGlass |-> FALSE, clearSettled |-> FALSE,
         changeAsked |-> TRUE, glass |-> Black, glassSettled |-> FALSE,
         synced |-> FALSE, bad |-> None]

(* A store checks itself against the glass as the pilot saw it *)
Store(t, what, v, via) ==
    LET shown == IF via = "notice" THEN t.glass = Seen("notice", what, v) /\ t.glassSettled
                 ELSE t.glass = Seen("block", what, v)
        heard == IF what = "dev" THEN t.heardDev = v ELSE t.heardAc = v
        ok == shown /\ heard /\ t.flight = "ground"
        u == IF what = "dev" THEN [t EXCEPT !.storedDev = v] ELSE [t EXCEPT !.storedAc = v]
    IN IF ok \/ u.bad # None THEN u ELSE [u EXCEPT !.bad = "stored what the glass did not show"]

CloseNotice(t) ==
    IF t.notice = None THEN t
    ELSE [t EXCEPT !.notice = None, !.changeAsked = TRUE,
                   !.noticeOnGlass = IF Fixed THEN FALSE ELSE t.noticeOnGlass]

OpenNotice(t, n) ==
    [t EXCEPT !.notice = n, !.changeAsked = TRUE,
              !.noticeOnGlass = IF Fixed THEN FALSE ELSE t.noticeOnGlass,
              !.devNotices = IF n = "dev" THEN t.devNotices + 1 ELSE t.devNotices]

LeaveMenu(t) ==
    [t EXCEPT !.mode = "page", !.sub = "settings", !.focus = "light", !.clearing = FALSE,
              !.changeAsked = TRUE]

UseNoticed(t) ==
    CASE t.notice = "dev" /\ DevDetected(t) = t.noticedDev /\ t.noticedDev \in FlarmIds
             /\ t.noticedDev # t.storedDev -> Store(t, "dev", t.noticedDev, "notice")
      [] t.notice = "ac" /\ AcDetected(t) = t.noticedAc /\ t.noticedAc \in Aircraft
             /\ t.noticedAc # t.storedAc -> Store(t, "ac", t.noticedAc, "notice")
      [] OTHER -> t

EraseFlarm(t) ==
    LET ok == t.glass = Seen("clear", "dev", t.storedDev) /\ t.glassSettled
        u == [t EXCEPT !.storedDev = None, !.clearing = FALSE]
    IN IF ok \/ u.bad # None THEN u ELSE [u EXCEPT !.bad = "erased before the question was read"]

UseBlock(t, what) ==
    LET v == IF what = "dev" THEN ToStore(t.devOnGlass, DevDetected(t), FlarmIds)
             ELSE ToStore(t.acOnGlass, AcDetected(t), Aircraft)
        stored == IF what = "dev" THEN t.storedDev ELSE t.storedAc
    IN IF v # None /\ v # stored
       THEN [Store(t, what, v, "block") EXCEPT !.sub = "settings",
                 !.focus = IF what = "dev" THEN "devrow" ELSE "acrow"]
       ELSE t

(* MenuEditor::act, and the clear question's ERASE *)
EditorPress(t) ==
    IF t.clearing
    THEN IF Fixed /\ ~(t.clearOnGlass /\ t.clearSettled) THEN t ELSE EraseFlarm(t)
    ELSE CASE t.sub = "settings" /\ t.focus = "devrow" -> [t EXCEPT !.sub = "flarm", !.focus = "block"]
           [] t.sub = "settings" /\ t.focus = "acrow" -> [t EXCEPT !.sub = "aircraft", !.focus = "block"]
           [] t.sub = "flarm" /\ t.focus = "block" -> UseBlock(t, "dev")
           [] t.sub = "flarm" /\ t.focus = "row" /\ t.storedDev # None ->
                  [t EXCEPT !.clearing = TRUE, !.clearOnGlass = FALSE, !.clearSettled = FALSE]
           [] t.sub = "aircraft" /\ t.focus = "block" -> UseBlock(t, "ac")
           [] OTHER -> t

(* MenuEditor::advance: the pad walks the rows, and past the last one leaves *)
EditorTap(t) ==
    IF t.clearing THEN [t EXCEPT !.clearing = FALSE]
    ELSE CASE t.sub = "settings" /\ t.focus = "light" -> [t EXCEPT !.focus = "acrow"]
           [] t.sub = "settings" /\ t.focus = "acrow" -> [t EXCEPT !.focus = "devrow"]
           [] t.sub = "settings" /\ t.focus = "devrow" -> [t EXCEPT !.focus = "rest"]
           [] t.sub = "settings" -> LeaveMenu(t)
           [] t.focus = "block" -> [t EXCEPT !.focus = "row"]
           [] OTHER -> [t EXCEPT !.sub = "settings",
                            !.focus = IF t.sub = "flarm" THEN "devrow" ELSE "acrow"]

(* ScreenService::press, with the notice's settle rule *)
Press(t) ==
    IF t.notice \in {"dev", "ac"}
    THEN IF ~(t.noticeOnGlass /\ t.noticeSettled) THEN t ELSE CloseNotice(UseNoticed(t))
    ELSE IF t.notice # None THEN CloseNotice(t)
    ELSE IF t.mode = "menu" THEN EditorPress(t)
    ELSE [t EXCEPT !.mode = "menu", !.sub = "settings", !.focus = "light", !.changeAsked = TRUE]

Tap(t) ==
    IF t.notice # None THEN CloseNotice(t)
    ELSE IF t.mode = "menu" THEN EditorTap(t)
    ELSE [t EXCEPT !.changeAsked = TRUE]

LongTouch(t) ==
    IF t.alarm THEN [t EXCEPT !.alarm = FALSE]
    ELSE IF t.notice # None THEN CloseNotice(t)
    ELSE IF t.mode = "menu" THEN LeaveMenu(t)
    ELSE t

Obey(t, g) ==
    CASE g = "press" -> Press(t) [] g = "tap" -> Tap(t) [] g = "long" -> LongTouch(t)
      [] OTHER -> t

(* MenuEditor::tick's timers: the question keeps after 30 s, the menu leaves after 60 s *)
EditorTimers(t, idle) ==
    IF ~idle \/ t.mode # "menu" THEN t
    ELSE IF t.clearing THEN [t EXCEPT !.clearing = FALSE]
    ELSE LeaveMenu(t)

SyncNotice(t, timeout, other) ==
    LET gone == (t.notice = "dev" /\ DevDetected(t) # t.noticedDev)
                \/ (t.notice = "ac" /\ AcDetected(t) # t.noticedAc)
        u == IF t.notice # None /\ (t.alarm \/ gone \/ timeout) THEN CloseNotice(t) ELSE t
    IN IF u.alarm THEN u
       ELSE IF other THEN OpenNotice(u, "other")
       ELSE IF u.notice = None /\ ~u.acNoticed /\ u.mode = "page"
               /\ AcDetected(u) \in Aircraft /\ AcDetected(u) # u.storedAc
            THEN OpenNotice([u EXCEPT !.noticedAc = AcDetected(u), !.acNoticed = TRUE], "ac")
       ELSE IF u.notice = None /\ ~u.devNoticed /\ u.mode = "page"
               /\ DevDetected(u) \in FlarmIds /\ DevDetected(u) # u.storedDev
            THEN OpenNotice([u EXCEPT !.noticedDev = DevDetected(u), !.devNoticed = TRUE], "dev")
       ELSE u

AlarmTakesMenu(t) == IF t.mode = "menu" /\ t.alarm THEN LeaveMenu(t) ELSE t

Pass ==
    \E g \in {"press", "tap", "long", "none"}, idle, timeout, other \in BOOLEAN :
        s' = [AlarmTakesMenu(SyncNotice(EditorTimers(Obey(s, g), idle), timeout, other))
                 EXCEPT !.synced = TRUE]

(* tick(): a wipe first, then the frame, which note_presented dates *)
Wipe ==
    /\ s.changeAsked
    /\ s' = [s EXCEPT !.changeAsked = FALSE, !.glass = Black, !.glassSettled = FALSE,
                      !.noticeOnGlass = FALSE,
                      !.devOnGlass = IF Fixed THEN None ELSE s.devOnGlass,
                      !.acOnGlass = IF Fixed THEN None ELSE s.acOnGlass,
                      !.clearOnGlass = IF Fixed THEN FALSE ELSE s.clearOnGlass]

Present ==
    /\ ~s.changeAsked
    /\ LET c == Content(s)
           menuShown == s.mode = "menu" /\ ~s.clearing /\ s.notice = None
       IN s' = [s EXCEPT
                 !.glass = c,
                 !.glassSettled = IF c = s.glass THEN s.glassSettled ELSE FALSE,
                 !.noticeSettled = IF s.notice # None /\ ~s.noticeOnGlass THEN FALSE
                                   ELSE s.noticeSettled,
                 !.noticeOnGlass = s.notice # None,
                 !.devOnGlass = IF Fixed THEN (IF c.k = "block" /\ c.n = "dev" THEN c.v ELSE None)
                                ELSE IF menuShown THEN DevDetected(s) ELSE s.devOnGlass,
                 !.acOnGlass = IF Fixed THEN (IF c.k = "block" /\ c.n = "ac" THEN c.v ELSE None)
                               ELSE IF menuShown THEN AcDetected(s) ELSE s.acOnGlass,
                 !.clearSettled = IF c.k = "clear" /\ ~s.clearOnGlass THEN FALSE
                                  ELSE s.clearSettled,
                 !.clearOnGlass = c.k = "clear"]

Render == Wipe \/ Present

Settle ==
    s' = [s EXCEPT !.glassSettled = TRUE, !.noticeSettled = s.noticeOnGlass,
                   !.clearSettled = s.clearOnGlass]

(* The detectors publish on the ground only: TrafficService and BravoService gate them *)
World ==
    \E f \in Flights, d \in FlarmIds \cup {None}, a \in Aircraft \cup {None}, al \in BOOLEAN :
        s' = [s EXCEPT !.flight = f,
                       !.heardDev = IF f = "ground" THEN d ELSE None,
                       !.heardAc = IF f = "ground" THEN a ELSE None,
                       !.alarm = al, !.synced = FALSE]

(* A phone's prompt takes the button: the editor leaves, and comes back on its first row *)
Prompt ==
    /\ s.mode = "menu"
    /\ s' = [s EXCEPT !.sub = "settings", !.focus = "light", !.clearing = FALSE,
                      !.changeAsked = TRUE, !.synced = FALSE]

Next == Pass \/ Render \/ Settle \/ World \/ Prompt

Spec == Init /\ [][Next]_s

(***************************************************************************)
(* What must hold                                                          *)
(***************************************************************************)
TypeOK ==
    /\ s.storedDev \in FlarmIds \cup {None}
    /\ s.storedAc \in Aircraft \cup {None}
    /\ s.notice \in {None, "other", "ac", "dev"}

(* Nothing is stored or erased that the pilot did not see on the glass *)
OnlyWhatWasShown == s.bad = None

(* The FLARM notice opens once a boot *)
OnceABoot == s.devNotices <= 1

(* After a pass, a notice that proposes still proposes what is heard on the ground *)
NoticeIsCurrent ==
    s.synced =>
        /\ s.notice = "dev" => s.flight = "ground" /\ DevDetected(s) = s.noticedDev
        /\ s.notice = "ac" => s.flight = "ground" /\ AcDetected(s) = s.noticedAc

(* After a pass, an alarm has the glass: no notice and no menu *)
AlarmHasTheGlass == s.synced /\ s.alarm => s.notice = None /\ s.mode = "page"

(* A notice never opens over a menu *)
NoNoticeOverAMenu == s.notice \in {"ac", "dev"} /\ s.synced => s.mode = "page"

=============================================================================
