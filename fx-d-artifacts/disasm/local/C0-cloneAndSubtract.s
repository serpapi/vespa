0000000000785f60 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)>:
  785f60:	stp	x29, x30, [sp, #-128]!
  785f64:	mov	x29, sp
  785f68:	stp	x19, x20, [sp, #16]
  785f6c:	mov	x20, x0
  785f70:	mov	x0, #0x28                  	// #40
  785f74:	mov	x19, x8
  785f78:	stp	x21, x22, [sp, #32]
  785f7c:	mov	w21, w2
  785f80:	str	x23, [sp, #48]
  785f84:	ldrb	w3, [x20, #12]
  785f88:	str	x1, [sp, #72]
  785f8c:	sub	w22, w3, w2
  785f90:	cmp	w2, w3
  785f94:	and	w22, w22, #0xff
  785f98:	csel	w22, w22, wzr, cc	// cc = lo, ul, last
  785f9c:	bl	676a10 <operator new(unsigned long)@plt>
  785fa0:	ldr	x2, [sp, #72]
  785fa4:	mov	w1, w22
  785fa8:	mov	w3, #0xffffffff            	// #-1
  785fac:	mov	x22, x0
  785fb0:	bl	6757a0 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)@plt>
  785fb4:	ldr	x1, [x20, #32]
  785fb8:	str	x22, [x19]
  785fbc:	add	x0, x1, #0x110
  785fc0:	ldr	x6, [x0]
  785fc4:	add	x1, x1, #0x240
  785fc8:	ldar	x2, [x1]
  785fcc:	ldr	x0, [x19]
  785fd0:	ldr	x22, [x0, #32]
  785fd4:	add	x9, x22, #0x110
  785fd8:	ldr	x0, [x9]
  785fdc:	cbnz	w0, 7861c0 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x260>
  785fe0:	add	x0, x22, #0x188
  785fe4:	ldar	w0, [x0]
  785fe8:	cbnz	w0, 7861c0 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x260>
  785fec:	mov	w23, w6
  785ff0:	cbnz	w6, 786044 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xe4>
  785ff4:	ldr	x0, [x19]
  785ff8:	mov	w1, #0x0                   	// #0
  785ffc:	ldr	x0, [x0, #32]
  786000:	bl	66f0a0 <search::AttributeVector::commit(search::CommitParam::UpdateStats)@plt>
  786004:	ldr	w2, [x20, #8]
  786008:	ldr	x3, [x19]
  78600c:	ldr	x1, [x20, #32]
  786010:	add	w2, w2, w21
  786014:	ldr	x0, [x3, #32]
  786018:	str	w2, [x3, #8]
  78601c:	add	x1, x1, #0x188
  786020:	ldar	w1, [x1]
  786024:	add	x0, x0, #0x188
  786028:	stlr	w1, [x0]
  78602c:	ldr	x23, [sp, #48]
  786030:	ldp	x21, x22, [sp, #32]
  786034:	mov	x0, x19
  786038:	ldp	x19, x20, [sp, #16]
  78603c:	ldp	x29, x30, [sp], #128
  786040:	ret
  786044:	ldr	x10, [x22, #552]
  786048:	and	x8, x6, #0xffffffff
  78604c:	add	x4, x22, #0x218
  786050:	cmp	x10, w6, uxtw
  786054:	b.cc	786148 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1e8>  // b.lo, b.ul, b.last
  786058:	mov	x0, x4
  78605c:	mov	x1, x8
  786060:	stp	x8, x9, [sp, #72]
  786064:	stp	x10, x6, [sp, #88]
  786068:	str	x2, [sp, #104]
  78606c:	bl	6761c0 <vespalib::RcuVectorBase<signed char>::unsafe_resize(unsigned long)@plt>
  786070:	ldp	x10, x6, [sp, #88]
  786074:	ldp	x8, x9, [sp, #72]
  786078:	sub	w6, w6, #0x1
  78607c:	ldr	x2, [sp, #104]
  786080:	cbz	x8, 78619c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x23c>
  786084:	cmp	w21, #0xff
  786088:	mov	w4, #0xff                  	// #255
  78608c:	csel	w4, w21, w4, ls	// ls = plast
  786090:	ldr	x7, [x22, #544]
  786094:	mov	w3, #0x0                   	// #0
  786098:	mov	x1, x2
  78609c:	add	x0, x8, x2
  7860a0:	and	w4, w4, #0xff
  7860a4:	sub	x7, x7, x2
  7860a8:	ldrb	w2, [x1]
  7860ac:	cmp	w2, w4
  7860b0:	b.ls	78612c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1cc>  // b.plast
  7860b4:	sub	w2, w2, w4
  7860b8:	cmp	w3, w2, uxtb
  7860bc:	csel	w3, w2, w3, ls	// ls = plast
  7860c0:	and	w3, w3, #0xff
  7860c4:	strb	w2, [x7, x1]
  7860c8:	add	x1, x1, #0x1
  7860cc:	cmp	x0, x1
  7860d0:	b.ne	7860a8 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x148>  // b.any
  7860d4:	dmb	ish
  7860d8:	str	x8, [x9]
  7860dc:	ldr	w0, [x22, #396]
  7860e0:	cmp	w0, w6
  7860e4:	b.hi	7860ec <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x18c>  // b.pmore
  7860e8:	str	w23, [x22, #396]
  7860ec:	str	w3, [sp, #72]
  7860f0:	mov	x0, x22
  7860f4:	cmp	x10, x8
  7860f8:	b.cs	786140 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1e0>  // b.hs, b.nlast
  7860fc:	bl	67b950 <search::AttributeVector::incGeneration()@plt>
  786100:	ldr	w3, [sp, #72]
  786104:	cmp	w3, #0xfd
  786108:	b.ls	785ff4 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x94>  // b.plast
  78610c:	adrp	x3, dba000 <_fini+0x25d10>
  786110:	adrp	x1, dba000 <_fini+0x25d10>
  786114:	adrp	x0, dba000 <_fini+0x25d10>
  786118:	add	x3, x3, #0x7e0
  78611c:	add	x1, x1, #0x5e8
  786120:	add	x0, x0, #0x850
  786124:	mov	w2, #0x53                  	// #83
  786128:	bl	6798c0 <__assert_fail@plt>
  78612c:	strb	wzr, [x7, x1]
  786130:	add	x1, x1, #0x1
  786134:	cmp	x1, x0
  786138:	b.ne	7860a8 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x148>  // b.any
  78613c:	b	7860d4 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x174>
  786140:	bl	66c7a0 <search::AttributeVector::reclaim_unused_memory()@plt>
  786144:	b	786100 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1a0>
  786148:	mov	x1, x8
  78614c:	mov	x0, x4
  786150:	str	x4, [sp, #72]
  786154:	stp	x8, x9, [sp, #88]
  786158:	stp	x10, x6, [sp, #104]
  78615c:	str	x2, [sp, #120]
  786160:	bl	672210 <vespalib::RcuVectorBase<signed char>::calcNewSize(unsigned long) const@plt>
  786164:	ldr	x4, [sp, #72]
  786168:	mov	x1, x0
  78616c:	mov	x0, x4
  786170:	str	x4, [sp, #80]
  786174:	bl	661c50 <vespalib::RcuVectorBase<signed char>::expand(unsigned long)@plt>
  786178:	ldp	x0, x8, [sp, #80]
  78617c:	mov	x1, x8
  786180:	str	x8, [sp, #72]
  786184:	bl	6761c0 <vespalib::RcuVectorBase<signed char>::unsafe_resize(unsigned long)@plt>
  786188:	ldp	x6, x2, [sp, #112]
  78618c:	ldp	x9, x10, [sp, #96]
  786190:	sub	w6, w6, #0x1
  786194:	ldr	x8, [sp, #72]
  786198:	b	786084 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x124>
  78619c:	dmb	ish
  7861a0:	str	xzr, [x9]
  7861a4:	ldr	w0, [x22, #396]
  7861a8:	cmp	w0, w6
  7861ac:	mov	x0, x22
  7861b0:	b.hi	7861b8 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x258>  // b.pmore
  7861b4:	str	w23, [x22, #396]
  7861b8:	bl	66c7a0 <search::AttributeVector::reclaim_unused_memory()@plt>
  7861bc:	b	785ff4 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x94>
  7861c0:	adrp	x3, dba000 <_fini+0x25d10>
  7861c4:	adrp	x1, dba000 <_fini+0x25d10>
  7861c8:	adrp	x0, dba000 <_fini+0x25d10>
  7861cc:	add	x3, x3, #0x6c8
  7861d0:	add	x1, x1, #0x4e8
  7861d4:	add	x0, x0, #0x540
  7861d8:	mov	w2, #0x53                  	// #83
  7861dc:	bl	6798c0 <__assert_fail@plt>
  7861e0:	mov	x19, x0
  7861e4:	mov	x1, #0x28                  	// #40
  7861e8:	mov	x0, x22
  7861ec:	bl	66e720 <operator delete(void*, unsigned long)@plt>
  7861f0:	mov	x0, x19
  7861f4:	bl	671810 <_Unwind_Resume@plt>
  7861f8:	ldr	x1, [x19]
  7861fc:	mov	x19, x0
  786200:	cbz	x1, 786214 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x2b4>
  786204:	ldr	x2, [x1]
  786208:	mov	x0, x1
  78620c:	ldr	x1, [x2, #40]
  786210:	blr	x1
  786214:	mov	x0, x19
  786218:	bl	671810 <_Unwind_Resume@plt>
  78621c:	nop
