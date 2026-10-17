0000000000785f60 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)>:
  785f60:	stp	x29, x30, [sp, #-128]!
  785f64:	mov	x29, sp
  785f68:	stp	x21, x22, [sp, #32]
  785f6c:	mov	x21, x0
  785f70:	mov	x0, #0x28                  	// #40
  785f74:	mov	w22, w2
  785f78:	stp	x19, x20, [sp, #16]
  785f7c:	mov	x19, x8
  785f80:	str	x23, [sp, #48]
  785f84:	ldrb	w3, [x21, #12]
  785f88:	str	x1, [sp, #72]
  785f8c:	sub	w20, w3, w2
  785f90:	cmp	w2, w3
  785f94:	and	w20, w20, #0xff
  785f98:	csel	w20, w20, wzr, cc	// cc = lo, ul, last
  785f9c:	bl	676a10 <operator new(unsigned long)@plt>
  785fa0:	ldr	x2, [sp, #72]
  785fa4:	mov	w1, w20
  785fa8:	mov	w3, #0xffffffff            	// #-1
  785fac:	mov	x20, x0
  785fb0:	bl	6757a0 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)@plt>
  785fb4:	ldr	x1, [x21, #32]
  785fb8:	str	x20, [x19]
  785fbc:	add	x0, x1, #0x110
  785fc0:	ldr	x2, [x0]
  785fc4:	add	x1, x1, #0x240
  785fc8:	ldar	x4, [x1]
  785fcc:	ldr	x0, [x19]
  785fd0:	ldr	x20, [x0, #32]
  785fd4:	add	x8, x20, #0x110
  785fd8:	ldr	x0, [x8]
  785fdc:	cbnz	w0, 7863c8 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x468>
  785fe0:	add	x0, x20, #0x188
  785fe4:	ldar	w0, [x0]
  785fe8:	cbnz	w0, 7863c8 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x468>
  785fec:	mov	w23, w2
  785ff0:	cbnz	w2, 786044 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xe4>
  785ff4:	ldr	x0, [x19]
  785ff8:	mov	w1, #0x0                   	// #0
  785ffc:	ldr	x0, [x0, #32]
  786000:	bl	66f0a0 <search::AttributeVector::commit(search::CommitParam::UpdateStats)@plt>
  786004:	ldr	w2, [x21, #8]
  786008:	ldr	x3, [x19]
  78600c:	ldr	x1, [x21, #32]
  786010:	add	w2, w2, w22
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
  786044:	ldr	x9, [x20, #552]
  786048:	and	x6, x2, #0xffffffff
  78604c:	add	x5, x20, #0x218
  786050:	cmp	x9, w2, uxtw
  786054:	b.cc	7862fc <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x39c>  // b.lo, b.ul, b.last
  786058:	mov	x0, x5
  78605c:	mov	x1, x6
  786060:	stp	x6, x8, [sp, #72]
  786064:	stp	x9, x2, [sp, #88]
  786068:	str	x4, [sp, #104]
  78606c:	bl	6761c0 <vespalib::RcuVectorBase<signed char>::unsafe_resize(unsigned long)@plt>
  786070:	ldp	x9, x2, [sp, #88]
  786074:	ldp	x6, x8, [sp, #72]
  786078:	sub	w5, w2, #0x1
  78607c:	ldr	x4, [sp, #104]
  786080:	cbz	x6, 786394 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x434>
  786084:	cmp	w22, #0xff
  786088:	sub	x1, x6, #0x1
  78608c:	ldr	x10, [x20, #544]
  786090:	mov	w2, #0xff                  	// #255
  786094:	csel	w2, w22, w2, ls	// ls = plast
  786098:	and	w2, w2, #0xff
  78609c:	cmp	x1, #0x6
  7860a0:	b.ls	786350 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x3f0>  // b.plast
  7860a4:	sub	x0, x10, #0x1
  7860a8:	sub	x0, x0, x4
  7860ac:	cmp	x0, #0xe
  7860b0:	b.ls	786350 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x3f0>  // b.plast
  7860b4:	cmp	x1, #0xe
  7860b8:	b.ls	7863b8 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x458>  // b.plast
  7860bc:	dup	v30.16b, w2
  7860c0:	and	x0, x6, #0xfffffff0
  7860c4:	mov	x1, x4
  7860c8:	add	x11, x4, x0
  7860cc:	sub	x7, x10, x4
  7860d0:	movi	v29.4s, #0x0
  7860d4:	nop
  7860d8:	nop
  7860dc:	nop
  7860e0:	ldr	q3, [x1]
  7860e4:	umin	v2.16b, v3.16b, v30.16b
  7860e8:	sub	v2.16b, v3.16b, v2.16b
  7860ec:	str	q2, [x7, x1]
  7860f0:	add	x1, x1, #0x10
  7860f4:	umax	v29.16b, v29.16b, v2.16b
  7860f8:	cmp	x1, x11
  7860fc:	b.ne	7860e0 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x180>  // b.any
  786100:	umaxv	b1, v29.16b
  786104:	umov	w1, v1.b[0]
  786108:	cmp	x6, x0
  78610c:	b.eq	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>  // b.none
  786110:	sub	x7, x6, x0
  786114:	sub	x11, x7, #0x1
  786118:	cmp	x11, #0x6
  78611c:	b.ls	786158 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1f8>  // b.plast
  786120:	mov	d0, v29.d[1]
  786124:	umax	v31.8b, v0.8b, v29.8b
  786128:	dup	v27.8b, w2
  78612c:	ldr	d28, [x4, x0]
  786130:	umin	v27.8b, v27.8b, v28.8b
  786134:	sub	v27.8b, v28.8b, v27.8b
  786138:	str	d27, [x10, x0]
  78613c:	umax	v27.8b, v27.8b, v31.8b
  786140:	umaxv	b27, v27.8b
  786144:	umov	w1, v27.b[0]
  786148:	tst	x7, #0x7
  78614c:	b.eq	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>  // b.none
  786150:	and	x7, x7, #0xfffffffffffffff8
  786154:	add	x0, x0, x7
  786158:	ldrb	w11, [x4, x0]
  78615c:	add	x12, x0, #0x1
  786160:	cmp	w11, w2
  786164:	csel	w7, w11, w2, cc	// cc = lo, ul, last
  786168:	sub	w11, w11, w7
  78616c:	and	w11, w11, #0xff
  786170:	cmp	w1, w11
  786174:	csel	w7, w1, w11, hi	// hi = pmore
  786178:	and	w1, w7, #0xff
  78617c:	strb	w11, [x10, x0]
  786180:	cmp	x6, x12
  786184:	b.ls	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>  // b.plast
  786188:	ldrb	w11, [x4, x12]
  78618c:	add	x13, x0, #0x2
  786190:	cmp	w11, w2
  786194:	csel	w14, w11, w2, cc	// cc = lo, ul, last
  786198:	sub	w11, w11, w14
  78619c:	and	w11, w11, #0xff
  7861a0:	cmp	w11, w1
  7861a4:	csel	w7, w11, w7, hi	// hi = pmore
  7861a8:	and	w1, w7, #0xff
  7861ac:	strb	w11, [x10, x12]
  7861b0:	cmp	x6, x13
  7861b4:	b.ls	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>  // b.plast
  7861b8:	ldrb	w11, [x4, x13]
  7861bc:	add	x12, x0, #0x3
  7861c0:	cmp	w11, w2
  7861c4:	csel	w14, w11, w2, cc	// cc = lo, ul, last
  7861c8:	sub	w11, w11, w14
  7861cc:	and	w11, w11, #0xff
  7861d0:	cmp	w11, w1
  7861d4:	csel	w7, w11, w7, hi	// hi = pmore
  7861d8:	and	w1, w7, #0xff
  7861dc:	strb	w11, [x10, x13]
  7861e0:	cmp	x6, x12
  7861e4:	b.ls	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>  // b.plast
  7861e8:	ldrb	w11, [x4, x12]
  7861ec:	add	x13, x0, #0x4
  7861f0:	cmp	w11, w2
  7861f4:	csel	w14, w11, w2, cc	// cc = lo, ul, last
  7861f8:	sub	w11, w11, w14
  7861fc:	and	w11, w11, #0xff
  786200:	cmp	w11, w1
  786204:	csel	w7, w11, w7, hi	// hi = pmore
  786208:	and	w1, w7, #0xff
  78620c:	strb	w11, [x10, x12]
  786210:	cmp	x6, x13
  786214:	b.ls	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>  // b.plast
  786218:	ldrb	w11, [x4, x13]
  78621c:	add	x12, x0, #0x5
  786220:	cmp	w11, w2
  786224:	csel	w14, w11, w2, cc	// cc = lo, ul, last
  786228:	sub	w11, w11, w14
  78622c:	and	w11, w11, #0xff
  786230:	cmp	w11, w1
  786234:	csel	w7, w11, w7, hi	// hi = pmore
  786238:	and	w1, w7, #0xff
  78623c:	strb	w11, [x10, x13]
  786240:	cmp	x6, x12
  786244:	b.ls	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>  // b.plast
  786248:	ldrb	w11, [x4, x12]
  78624c:	add	x0, x0, #0x6
  786250:	cmp	w11, w2
  786254:	csel	w13, w11, w2, cc	// cc = lo, ul, last
  786258:	sub	w11, w11, w13
  78625c:	and	w11, w11, #0xff
  786260:	cmp	w11, w1
  786264:	csel	w7, w11, w7, hi	// hi = pmore
  786268:	and	w1, w7, #0xff
  78626c:	strb	w11, [x10, x12]
  786270:	cmp	x6, x0
  786274:	b.ls	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>  // b.plast
  786278:	ldrb	w4, [x4, x0]
  78627c:	cmp	w4, w2
  786280:	csel	w2, w4, w2, cc	// cc = lo, ul, last
  786284:	sub	w2, w4, w2
  786288:	and	w2, w2, #0xff
  78628c:	cmp	w1, w2
  786290:	csel	w7, w7, w2, hi	// hi = pmore
  786294:	and	w1, w7, #0xff
  786298:	strb	w2, [x10, x0]
  78629c:	dmb	ish
  7862a0:	str	x6, [x8]
  7862a4:	ldr	w0, [x20, #396]
  7862a8:	cmp	w0, w5
  7862ac:	b.hi	7862b4 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x354>  // b.pmore
  7862b0:	str	w23, [x20, #396]
  7862b4:	str	w1, [sp, #72]
  7862b8:	mov	x0, x20
  7862bc:	cmp	x9, x6
  7862c0:	b.cs	7862f4 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x394>  // b.hs, b.nlast
  7862c4:	bl	67b950 <search::AttributeVector::incGeneration()@plt>
  7862c8:	ldr	w1, [sp, #72]
  7862cc:	cmp	w1, #0xfd
  7862d0:	b.ls	785ff4 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x94>  // b.plast
  7862d4:	adrp	x3, dba000 <_fini+0x25b10>
  7862d8:	adrp	x1, dba000 <_fini+0x25b10>
  7862dc:	adrp	x0, dba000 <_fini+0x25b10>
  7862e0:	add	x3, x3, #0x9e0
  7862e4:	add	x1, x1, #0x7e8
  7862e8:	add	x0, x0, #0xa50
  7862ec:	mov	w2, #0x54                  	// #84
  7862f0:	bl	6798c0 <__assert_fail@plt>
  7862f4:	bl	66c7a0 <search::AttributeVector::reclaim_unused_memory()@plt>
  7862f8:	b	7862c8 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x368>
  7862fc:	mov	x1, x6
  786300:	mov	x0, x5
  786304:	str	x5, [sp, #72]
  786308:	stp	x6, x8, [sp, #88]
  78630c:	stp	x9, x2, [sp, #104]
  786310:	str	x4, [sp, #120]
  786314:	bl	672210 <vespalib::RcuVectorBase<signed char>::calcNewSize(unsigned long) const@plt>
  786318:	ldr	x5, [sp, #72]
  78631c:	mov	x1, x0
  786320:	mov	x0, x5
  786324:	str	x5, [sp, #80]
  786328:	bl	661c50 <vespalib::RcuVectorBase<signed char>::expand(unsigned long)@plt>
  78632c:	ldp	x0, x6, [sp, #80]
  786330:	mov	x1, x6
  786334:	str	x6, [sp, #72]
  786338:	bl	6761c0 <vespalib::RcuVectorBase<signed char>::unsafe_resize(unsigned long)@plt>
  78633c:	ldp	x2, x4, [sp, #112]
  786340:	ldp	x8, x9, [sp, #96]
  786344:	sub	w5, w2, #0x1
  786348:	ldr	x6, [sp, #72]
  78634c:	b	786084 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x124>
  786350:	mov	x7, x4
  786354:	mov	w1, #0x0                   	// #0
  786358:	add	x4, x6, x4
  78635c:	sub	x10, x10, x7
  786360:	ldrb	w0, [x7]
  786364:	cmp	w2, w0
  786368:	csel	w11, w2, w0, cc	// cc = lo, ul, last
  78636c:	sub	w0, w0, w11
  786370:	and	w0, w0, #0xff
  786374:	cmp	w1, w0
  786378:	csel	w1, w1, w0, hi	// hi = pmore
  78637c:	and	w1, w1, #0xff
  786380:	strb	w0, [x10, x7]
  786384:	add	x7, x7, #0x1
  786388:	cmp	x4, x7
  78638c:	b.ne	786360 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x400>  // b.any
  786390:	b	78629c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x33c>
  786394:	dmb	ish
  786398:	str	xzr, [x8]
  78639c:	ldr	w0, [x20, #396]
  7863a0:	cmp	w0, w5
  7863a4:	mov	x0, x20
  7863a8:	b.hi	7863b0 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x450>  // b.pmore
  7863ac:	str	w23, [x20, #396]
  7863b0:	bl	66c7a0 <search::AttributeVector::reclaim_unused_memory()@plt>
  7863b4:	b	785ff4 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x94>
  7863b8:	movi	v31.2s, #0x0
  7863bc:	mov	x0, #0x0                   	// #0
  7863c0:	mov	x7, x6
  7863c4:	b	786128 <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1c8>
  7863c8:	adrp	x3, dba000 <_fini+0x25b10>
  7863cc:	adrp	x1, dba000 <_fini+0x25b10>
  7863d0:	adrp	x0, dba000 <_fini+0x25b10>
  7863d4:	add	x3, x3, #0x8c8
  7863d8:	add	x1, x1, #0x6e8
  7863dc:	add	x0, x0, #0x740
  7863e0:	mov	w2, #0x53                  	// #83
  7863e4:	bl	6798c0 <__assert_fail@plt>
  7863e8:	mov	x19, x0
  7863ec:	mov	x1, #0x28                  	// #40
  7863f0:	mov	x0, x20
  7863f4:	bl	66e720 <operator delete(void*, unsigned long)@plt>
  7863f8:	mov	x0, x19
  7863fc:	bl	671810 <_Unwind_Resume@plt>
  786400:	ldr	x1, [x19]
  786404:	mov	x19, x0
  786408:	cbz	x1, 78641c <search::FixedSourceSelector::cloneAndSubtract(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x4bc>
  78640c:	ldr	x2, [x1]
  786410:	mov	x0, x1
  786414:	ldr	x1, [x2, #40]
  786418:	blr	x1
  78641c:	mov	x0, x19
  786420:	bl	671810 <_Unwind_Resume@plt>
