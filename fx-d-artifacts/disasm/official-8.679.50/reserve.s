  7844e0:      	stp	x29, x30, [sp, #-0x40]!
  7844e4:      	mov	x29, sp
  7844e8:      	stp	x19, x20, [sp, #0x10]
  7844ec:      	mov	x19, x0
  7844f0:      	ldr	x0, [x0, #0x20]
  7844f4:      	add	x2, x0, #0x110
  7844f8:      	ldr	x2, [x2]
  7844fc:      	add	w20, w1, #0x1
  784500:      	cmp	w20, w2
  784504:      	b.hi	0x78454c <search::FixedSourceSelector::reserve(unsigned int)+0x6c>
  784508:      	add	x0, x0, #0x188
  78450c:      	ldar	w1, [x0]
  784510:      	cmp	w20, w1
  784514:      	b.ls	0x784540 <search::FixedSourceSelector::reserve(unsigned int)+0x60>
  784518:      	nop
  78451c:      	nop
  784520:      	ldr	x0, [x19, #0x20]
  784524:      	ldr	x2, [x0, #0x220]
  784528:      	ldrb	w0, [x19, #0xc]
  78452c:      	add	x2, x2, x1
  784530:      	strb	w0, [x2]
  784534:      	add	x1, x1, #0x1
  784538:      	cmp	w20, w1
  78453c:      	b.hi	0x784520 <search::FixedSourceSelector::reserve(unsigned int)+0x40>
  784540:      	ldp	x19, x20, [sp, #0x10]
  784544:      	ldp	x29, x30, [sp], #0x40
  784548:      	ret
  78454c:      	stp	x21, x22, [sp, #0x20]
  784550:      	mov	w21, w1
  784554:      	add	x1, sp, #0x3c
  784558:      	str	wzr, [sp, #0x3c]
  78455c:      	bl	0x662b10 <_ZN6search27SingleValueNumericAttributeINS_24IntegerAttributeTemplateIaEEE6addDocERj@plt>
  784560:      	ldr	w0, [sp, #0x3c]
  784564:      	cmp	w21, w0
  784568:      	b.ls	0x784584 <search::FixedSourceSelector::reserve(unsigned int)+0xa4>
  78456c:      	ldr	x0, [x19, #0x20]
  784570:      	add	x1, sp, #0x3c
  784574:      	bl	0x662b10 <_ZN6search27SingleValueNumericAttributeINS_24IntegerAttributeTemplateIaEEE6addDocERj@plt>
  784578:      	ldr	w0, [sp, #0x3c]
  78457c:      	cmp	w0, w21
  784580:      	b.lo	0x78456c <search::FixedSourceSelector::reserve(unsigned int)+0x8c>
  784584:      	ldr	x0, [x19, #0x20]
  784588:      	ldp	x21, x22, [sp, #0x20]
  78458c:      	b	0x784508 <search::FixedSourceSelector::reserve(unsigned int)+0x28>
