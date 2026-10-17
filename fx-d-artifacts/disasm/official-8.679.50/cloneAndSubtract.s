  7849a0:      	stp	x29, x30, [sp, #-0x50]!
  7849a4:      	mov	x29, sp
  7849a8:      	stp	x21, x22, [sp, #0x20]
  7849ac:      	mov	x21, x0
  7849b0:      	stp	x19, x20, [sp, #0x10]
  7849b4:      	mov	w20, w2
  7849b8:      	mov	x19, x8
  7849bc:      	ldr	x0, [x0, #0x20]
  7849c0:      	str	x23, [sp, #0x30]
  7849c4:      	ldrb	w3, [x21, #0xc]
  7849c8:      	str	x1, [sp, #0x48]
  7849cc:      	add	x0, x0, #0x110
  7849d0:      	sub	w22, w3, w2
  7849d4:      	cmp	w2, w3
  7849d8:      	and	w22, w22, #0xff
  7849dc:      	csel	w22, w22, wzr, lo
  7849e0:      	ldr	x23, [x0]
  7849e4:      	mov	x0, #0x28               // =40
  7849e8:      	bl	0x675640 <_Znwm@plt>
  7849ec:      	ldr	x2, [sp, #0x48]
  7849f0:      	mov	w1, w22
  7849f4:      	sub	w3, w23, #0x1
  7849f8:      	mov	x22, x0
  7849fc:      	bl	0x6743e0 <_ZN6search19FixedSourceSelectorC1EhRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEj@plt>
  784a00:      	mov	w1, #0x0                // =0
  784a04:      	str	x22, [x19]
  784a08:      	b	0x784a34 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> const&, unsigned int)+0x94>
  784a0c:      	sub	w0, w0, w20
  784a10:      	and	w0, w0, #0xff
  784a14:      	cmp	w0, #0xfd
  784a18:      	b.hi	0x784abc <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> const&, unsigned int)+0x11c>
  784a1c:      	ldr	x2, [x19]
  784a20:      	ldr	x2, [x2, #0x20]
  784a24:      	ldr	x2, [x2, #0x220]
  784a28:      	add	x2, x2, x3
  784a2c:      	strb	w0, [x2]
  784a30:      	add	w1, w1, #0x1
  784a34:      	ldr	x0, [x21, #0x20]
  784a38:      	add	x2, x0, #0x110
  784a3c:      	ldr	x2, [x2]
  784a40:      	cmp	w1, w2
  784a44:      	b.hs	0x784a6c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> const&, unsigned int)+0xcc>
  784a48:      	add	x0, x0, #0x240
  784a4c:      	mov	w3, w1
  784a50:      	ldar	x0, [x0]
  784a54:      	add	x0, x0, x3
  784a58:      	ldrb	w0, [x0]
  784a5c:      	cmp	w20, w0
  784a60:      	b.lo	0x784a0c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> const&, unsigned int)+0x6c>
  784a64:      	mov	w0, #0x0                // =0
  784a68:      	b	0x784a1c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> const&, unsigned int)+0x7c>
  784a6c:      	ldr	x0, [x19]
  784a70:      	mov	w1, #0x0                // =0
  784a74:      	ldr	x0, [x0, #0x20]
  784a78:      	bl	0x66dd20 <_ZN6search15AttributeVector6commitENS_11CommitParam11UpdateStatsE@plt>
  784a7c:      	ldr	w2, [x21, #0x8]
  784a80:      	ldr	x3, [x19]
  784a84:      	ldr	x1, [x21, #0x20]
  784a88:      	add	w2, w2, w20
  784a8c:      	ldr	x0, [x3, #0x20]
  784a90:      	str	w2, [x3, #0x8]
  784a94:      	add	x1, x1, #0x188
  784a98:      	ldar	w1, [x1]
  784a9c:      	add	x0, x0, #0x188
  784aa0:      	stlr	w1, [x0]
  784aa4:      	ldr	x23, [sp, #0x30]
  784aa8:      	ldp	x21, x22, [sp, #0x20]
  784aac:      	mov	x0, x19
  784ab0:      	ldp	x19, x20, [sp, #0x10]
  784ab4:      	ldp	x29, x30, [sp], #0x50
  784ab8:      	ret
  784abc:      	adrp	x3, 0xdb6000 <u_strFromUTF8_60+0xdb6000>
  784ac0:      	adrp	x1, 0xdb6000 <u_strFromUTF8_60+0xdb6000>
  784ac4:      	adrp	x0, 0xdb6000 <u_strFromUTF8_60+0xdb6000>
  784ac8:      	add	x3, x3, #0x630
  784acc:      	add	x1, x1, #0x528
  784ad0:      	add	x0, x0, #0x6a0
  784ad4:      	mov	w2, #0x3b               // =59
  784ad8:      	bl	0x678500 <__assert_fail@plt>
  784adc:      	mov	x19, x0
  784ae0:      	mov	x1, #0x28               // =40
  784ae4:      	mov	x0, x22
  784ae8:      	bl	0x66d3a0 <_ZdlPvm@plt>
  784aec:      	mov	x0, x19
  784af0:      	bl	0x6704a0 <_Unwind_Resume@plt>
  784af4:      	ldr	x1, [x19]
  784af8:      	mov	x19, x0
  784afc:      	cbz	x1, 0x784b10 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> const&, unsigned int)+0x170>
  784b00:      	ldr	x2, [x1]
  784b04:      	mov	x0, x1
  784b08:      	ldr	x1, [x2, #0x28]
  784b0c:      	blr	x1
  784b10:      	mov	x0, x19
  784b14:      	bl	0x6704a0 <_Unwind_Resume@plt>
  784b18:      	nop
  784b1c:      	nop
