------------------------------- MODULE Aboard -------------------------------
(***************************************************************************)
(* traffic::Aboard, core/traffic/aboard.h: which emitter is aboard our own *)
(* aircraft. Bravo runs it on 1090 MHz, the Go on 868 MHz.                 *)
(*                                                                         *)
(* Hear is one frame at the current instant. Tick moves the clock one unit *)
(* and runs tick(). The table, the admission, the damping and the leader   *)
(* loop are written as the code runs them, and the properties say the same *)
(* thing declaratively: the emitter named is the one that has qualified    *)
(* for the hold at every tick since, and one that has is named.            *)
(***************************************************************************)
EXTENDS Integers, FiniteSets

CONSTANTS Keys, Tracked, HeardLevels, Floor, Lead, Hold, MaxAge, Damping, Sixteenths

None == 0
Slots == 1..Tracked

(* C++ integer division truncates towards zero *)
Quot(a, b) == IF a >= 0 THEN a \div b ELSE -((-a) \div b)

Free == [used |-> FALSE, key |-> None, level |-> 0, age |-> 0]

VARIABLES slot, leading, leaderKey, leadingFor, held, runs, ticked

vars == <<slot, leading, leaderKey, leadingFor, held, runs, ticked>>

Live(i) == slot[i].used /\ slot[i].age <= MaxAge

Init ==
    /\ slot = [i \in Slots |-> Free]
    /\ leading = FALSE
    /\ leaderKey = None
    /\ leadingFor = 0
    /\ held = FALSE
    /\ runs = [k \in Keys |-> 0]
    /\ ticked = FALSE

(* find(), then admit(): the first slot free or gone quiet, else the faintest *)
Found(k) == {i \in Slots : slot[i].used /\ slot[i].key = k}

RECURSIVE Faintest(_)
Faintest(n) ==
    IF n = 1 THEN 1
    ELSE LET best == Faintest(n - 1)
         IN IF slot[n].level < slot[best].level THEN n ELSE best

RECURSIVE FirstOpen(_)
FirstOpen(n) ==
    IF n > Tracked THEN None
    ELSE IF ~Live(n) THEN n ELSE FirstOpen(n + 1)

Admitted == IF FirstOpen(1) # None THEN FirstOpen(1) ELSE Faintest(Tracked)

Hear(k, dbm) ==
    LET level == dbm * Sixteenths
    IN /\ IF Found(k) # {}
          THEN LET i == CHOOSE j \in Found(k) : TRUE
               IN slot' = [slot EXCEPT ![i].level = @ + Quot(level - @, Damping), ![i].age = 0]
          ELSE slot' = [slot EXCEPT ![Admitted] = [used |-> TRUE, key |-> k, level |-> level,
                                                   age |-> 0]]
       /\ ticked' = FALSE
       /\ UNCHANGED <<leading, leaderKey, leadingFor, held, runs>>

(* leader(): the loop over the slots, keeping the loudest and the runner-up *)
RECURSIVE Loop(_, _, _, _)
Loop(n, t, loud, runner) ==
    IF n > Tracked THEN <<loud, runner>>
    ELSE IF ~t[n].used \/ t[n].age > MaxAge THEN Loop(n + 1, t, loud, runner)
    ELSE IF loud = None \/ t[n].level > t[loud].level THEN Loop(n + 1, t, n, loud)
    ELSE IF runner = None \/ t[n].level > t[runner].level THEN Loop(n + 1, t, loud, n)
    ELSE Loop(n + 1, t, loud, runner)

Leader(t) ==
    LET r == Loop(1, t, None, None)
        loud == r[1]
        runner == r[2]
    IN IF loud = None \/ t[loud].level < Floor * Sixteenths THEN None
       ELSE IF runner # None /\ t[loud].level - t[runner].level < Lead * Sixteenths THEN None
       ELSE t[loud].key

(* What the README says qualifies, with no loop: loud enough and a clear lead over every other *)
Qualifies(k, t) ==
    \E i \in Slots :
        /\ t[i].used /\ t[i].age <= MaxAge /\ t[i].key = k
        /\ t[i].level >= Floor * Sixteenths
        /\ \A j \in Slots \ {i} :
               (t[j].used /\ t[j].age <= MaxAge) => t[i].level - t[j].level >= Lead * Sixteenths

Tick ==
    LET aged == [i \in Slots |-> IF slot[i].used THEN [slot[i] EXCEPT !.age = @ + 1] ELSE slot[i]]
        kept == [i \in Slots |-> IF aged[i].used /\ aged[i].age > MaxAge THEN Free ELSE aged[i]]
        loud == Leader(kept)
        again == leading /\ loud = leaderKey
        for == IF again THEN IF leadingFor < Hold THEN leadingFor + 1 ELSE Hold ELSE 0
    IN /\ slot' = kept
       /\ leading' = (loud # None)
       /\ leaderKey' = IF loud = None THEN leaderKey ELSE loud
       /\ leadingFor' = IF loud = None THEN 0 ELSE for
       /\ held' = (loud # None /\ for >= Hold)
       /\ runs' = [k \in Keys |-> IF Qualifies(k, kept) THEN IF runs[k] <= Hold THEN runs[k] + 1
                                                             ELSE runs[k]
                                  ELSE 0]
       /\ ticked' = TRUE

Next ==
    \/ \E k \in Keys, dbm \in HeardLevels : Hear(k, dbm)
    \/ Tick

Spec == Init /\ [][Next]_vars

(* held(): the leader's emitter, while it is still in the table *)
Out == IF held /\ Found(leaderKey) # {} THEN leaderKey ELSE None

(* The loop names exactly the emitter the declarative rule names *)
LoopIsTheRule ==
    \A k \in Keys : (Leader(slot) = k) <=> Qualifies(k, slot)

(* Right after a tick, the emitter named has qualified at every tick for the whole hold *)
NamedOnlyAfterTheHold == ticked => \A k \in Keys : Out = k => runs[k] > Hold

(* Right after a tick, an emitter that has qualified for the whole hold is named *)
NamedOnceHeld == ticked => \A k \in Keys : runs[k] > Hold => Out = k

(* Nothing is named that is not live and loud enough *)
NamedIsLive == ticked => (Out # None => Qualifies(Out, slot))

TypeOK ==
    /\ \A i \in Slots : slot[i].age \in 0..(MaxAge + 1)
    /\ leadingFor \in 0..Hold
    /\ Cardinality({i \in Slots : slot[i].used}) <= Tracked
    /\ \A i, j \in Slots : (i # j /\ slot[i].used /\ slot[j].used) => slot[i].key # slot[j].key

=============================================================================
