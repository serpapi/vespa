  3fc920:      	stp	x29, x30, [sp, #-0x20]!
  3fc924:      	mov	x29, sp
  3fc928:      	stp	x19, x20, [sp, #0x10]
  3fc92c:      	mov	x19, x0
  3fc930:      	add	x20, x0, #0x10
  3fc934:      	ldr	x0, [x0, #0x18]
  3fc938:      	mov	x1, x0
  3fc93c:      	b	0x3fc964 <vespalib::GenerationHandler::update_oldest_used_generation()+0x44>
  3fc940:      	bl	0x202ad0 <_ZN8vespalib14GenerationHold10setInvalidEv@plt>
  3fc944:      	tbz	w0, #0x0, 0x3fc98c <vespalib::GenerationHandler::update_oldest_used_generation()+0x6c>
  3fc948:      	ldr	x2, [x19, #0x18]
  3fc94c:      	ldr	x1, [x2, #0x10]
  3fc950:      	cbz	x1, 0x3fc9ac <vespalib::GenerationHandler::update_oldest_used_generation()+0x8c>
  3fc954:      	str	x1, [x19, #0x18]
  3fc958:      	ldp	x0, x3, [x19, #0x18]
  3fc95c:      	str	x3, [x2, #0x10]
  3fc960:      	str	x2, [x19, #0x20]
  3fc964:      	ldr	x2, [x20]
  3fc968:      	cmp	x1, x2
  3fc96c:      	b.ne	0x3fc940 <vespalib::GenerationHandler::update_oldest_used_generation()+0x20>
  3fc970:      	add	x0, x0, #0x8
  3fc974:      	ldar	x0, [x0]
  3fc978:      	add	x19, x19, #0x8
  3fc97c:      	str	x0, [x19]
  3fc980:      	ldp	x19, x20, [sp, #0x10]
  3fc984:      	ldp	x29, x30, [sp], #0x20
  3fc988:      	ret
  3fc98c:      	ldr	x0, [x19, #0x18]
  3fc990:      	add	x0, x0, #0x8
  3fc994:      	ldar	x0, [x0]
  3fc998:      	add	x19, x19, #0x8
  3fc99c:      	str	x0, [x19]
  3fc9a0:      	ldp	x19, x20, [sp, #0x10]
  3fc9a4:      	ldp	x29, x30, [sp], #0x20
  3fc9a8:      	ret
  3fc9ac:      	adrp	x3, 0x4b4000 <write@GLIBC_2.17+0x4b4000>
  3fc9b0:      	adrp	x1, 0x4b4000 <write@GLIBC_2.17+0x4b4000>
  3fc9b4:      	adrp	x0, 0x4b4000 <write@GLIBC_2.17+0x4b4000>
  3fc9b8:      	add	x3, x3, #0x938
  3fc9bc:      	add	x1, x1, #0x980
  3fc9c0:      	add	x0, x0, #0x9f0
  3fc9c4:      	mov	w2, #0x11               // =17
  3fc9c8:      	bl	0x1ff700 <__assert_fail@plt>
