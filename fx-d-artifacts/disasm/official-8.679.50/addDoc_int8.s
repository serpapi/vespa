  94bfe0:      	stp	x29, x30, [sp, #-0x50]!
  94bfe4:      	mov	x29, sp
  94bfe8:      	stp	x19, x20, [sp, #0x10]
  94bfec:      	mov	x19, x0
  94bff0:      	mov	x20, x1
  94bff4:      	stp	x21, x22, [sp, #0x20]
  94bff8:      	ldr	x21, [x0, #0x228]
  94bffc:      	str	x23, [sp, #0x30]
  94c000:      	add	x23, x0, #0x220
  94c004:      	ldr	x22, [x23, #0x18]
  94c008:      	bl	0x662020 <_ZNK6search15AttributeVector9isMutableEv@plt>
  94c00c:      	ldr	x2, [x23, #0x18]
  94c010:      	eor	w0, w0, #0x1
  94c014:      	sbfiz	w0, w0, #7, #1
  94c018:      	ldr	x1, [x19, #0x228]
  94c01c:      	strb	w0, [sp, #0x4f]
  94c020:      	cmp	x2, x1
  94c024:      	b.hs	0x94c098 <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char>>::addDoc(unsigned int&)+0xb8>
  94c028:      	add	x3, x2, #0x1
  94c02c:      	cmp	x1, x3
  94c030:      	b.lo	0x94c0a8 <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char>>::addDoc(unsigned int&)+0xc8>
  94c034:      	ldr	x1, [x19, #0x220]
  94c038:      	str	x3, [x23, #0x18]
  94c03c:      	strb	w0, [x1, x2]
  94c040:      	dmb	ish
  94c044:      	add	x0, x19, #0x110
  94c048:      	ldr	x1, [x0]
  94c04c:      	add	x1, x1, #0x1
  94c050:      	str	x1, [x0]
  94c054:      	ldr	x1, [x0]
  94c058:      	sub	w0, w1, #0x1
  94c05c:      	str	w0, [x20]
  94c060:      	ldr	w2, [x19, #0x18c]
  94c064:      	cmp	w0, w2
  94c068:      	b.lo	0x94c070 <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char>>::addDoc(unsigned int&)+0x90>
  94c06c:      	str	w1, [x19, #0x18c]
  94c070:      	mov	x0, x19
  94c074:      	cmp	x22, x21
  94c078:      	b.eq	0x94c0d4 <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char>>::addDoc(unsigned int&)+0xf4>
  94c07c:      	bl	0x66b430 <_ZN6search15AttributeVector21reclaim_unused_memoryEv@plt>
  94c080:      	ldr	x23, [sp, #0x30]
  94c084:      	mov	w0, #0x1                // =1
  94c088:      	ldp	x19, x20, [sp, #0x10]
  94c08c:      	ldp	x21, x22, [sp, #0x20]
  94c090:      	ldp	x29, x30, [sp], #0x50
  94c094:      	ret
  94c098:      	add	x1, sp, #0x4f
  94c09c:      	add	x0, x19, #0x218
  94c0a0:      	bl	0x670160 <_ZN8vespalib13RcuVectorBaseIaE15expandAndInsertERKa@plt>
  94c0a4:      	b	0x94c040 <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char>>::addDoc(unsigned int&)+0x60>
  94c0a8:      	clz	x2, x2
  94c0ac:      	mov	w0, #0x3f               // =63
  94c0b0:      	sub	w0, w0, w2
  94c0b4:      	mov	x1, #0x2                // =2
  94c0b8:      	lsl	x1, x1, x0
  94c0bc:      	mov	x0, x23
  94c0c0:      	bl	0x66ffd0 <_ZN8vespalib5ArrayIaE7reserveEm@plt>
  94c0c4:      	ldr	x2, [x23, #0x18]
  94c0c8:      	ldrsb	w0, [sp, #0x4f]
  94c0cc:      	add	x3, x2, #0x1
  94c0d0:      	b	0x94c034 <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char>>::addDoc(unsigned int&)+0x54>
  94c0d4:      	bl	0x67a580 <_ZN6search15AttributeVector13incGenerationEv@plt>
  94c0d8:      	ldr	x23, [sp, #0x30]
  94c0dc:      	mov	w0, #0x1                // =1
  94c0e0:      	ldp	x19, x20, [sp, #0x10]
  94c0e4:      	ldp	x21, x22, [sp, #0x20]
  94c0e8:      	ldp	x29, x30, [sp], #0x50
  94c0ec:      	ret
  94c0f0:      	nop
  94c0f4:      	nop
  94c0f8:      	nop
  94c0fc:      	nop
