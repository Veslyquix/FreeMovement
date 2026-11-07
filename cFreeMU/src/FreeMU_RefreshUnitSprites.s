.thumb

@ RefreshUnitSprites jumps here from $271E0, in place of the
@ "gBmMapUnit[y][x] == 0" check (TrapRework hooks the end of the function).
@ r6 = unit, r3 is free (jumpToHack uses it)

.global FMU_RefreshUnitSpritesHook
.type FMU_RefreshUnitSpritesHook, %function
FMU_RefreshUnitSpritesHook:
	mov		r0, r6
	ldr		r3, =Fmu_ShouldDrawUnitSprite
	bl		.Lbx_r3

	@ what vanilla has in r2/r3 at this point
	ldr		r3, [r6, #0xC]
	mov		r2, #0x11
	ldrsb	r2, [r6, r2]

	cmp		r0, #0
	beq		.Lskip
	ldr		r0, =0x080271FB
	bx		r0
.Lskip:
	ldr		r0, =0x08027283
	bx		r0

.Lbx_r3:
	bx		r3

.ltorg
.align
