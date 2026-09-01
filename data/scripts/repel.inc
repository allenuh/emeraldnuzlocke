@ Run from UpdateRepelCounter on the step a Repel's counter reaches zero.
@
@ lockall/releaseall are spelled out here because the yes/no prompt needs them:
@ the message this replaced got its locking for free from MSGBOX_SIGN, and this
@ script fires mid-step rather than from an interaction.
EventScript_RepelWoreOff::
	lockall
	specialvar VAR_RESULT, TryPrepareRepelReuse
	goto_if_eq VAR_RESULT, FALSE, EventScript_RepelWoreOffNoneLeft
	msgbox Text_UseAnotherRepel, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, EventScript_RepelWoreOffDone
	special UseRepelFromPrompt
	waitse
	msgbox Text_RepelUsedAgain, MSGBOX_DEFAULT
EventScript_RepelWoreOffDone::
	releaseall
	end

EventScript_RepelWoreOffNoneLeft::
	msgbox Text_RepelWoreOff, MSGBOX_DEFAULT
	releaseall
	end

Text_RepelWoreOff:
	.string "REPEL's effect wore off…$"

Text_UseAnotherRepel:
	.string "REPEL's effect wore off…\p"
	.string "Would you like to use another\n"
	.string "{STR_VAR_1}?$"

Text_RepelUsedAgain:
	.string "{PLAYER} used the {STR_VAR_1}.$"
