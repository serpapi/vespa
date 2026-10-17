  6e5800:      	stp	x29, x30, [sp, #-0x20]!
  6e5804:      	mov	x29, sp
  6e5808:      	str	x19, [sp, #0x10]
  6e580c:      	mov	x19, x0
  6e5810:      	add	x0, x0, #0xa8
  6e5814:      	bl	0x660cb0 <_ZN8vespalib17GenerationHandler29update_oldest_used_generationEv@plt>
  6e5818:      	mov	x0, x19
  6e581c:      	ldr	x1, [x0], #0xb0
  6e5820:      	ldr	x2, [x1, #0x238]
  6e5824:      	ldr	x1, [x0]
  6e5828:      	adrp	x0, 0x6e5000 <search::AttributeSaver::save(search::IAttributeSaveTarget&)+0x5c>
  6e582c:      	add	x0, x0, #0x3c0
  6e5830:      	cmp	x2, x0
  6e5834:      	b.ne	0x6e5844 <search::AttributeVector::reclaim_unused_memory() (.localalias)+0x44>
  6e5838:      	ldr	x19, [sp, #0x10]
  6e583c:      	ldp	x29, x30, [sp], #0x20
  6e5840:      	ret
  6e5844:      	mov	x0, x19
  6e5848:      	ldr	x19, [sp, #0x10]
  6e584c:      	mov	x16, x2
  6e5850:      	ldp	x29, x30, [sp], #0x20
  6e5854:      	br	x16
  6e5858:      	nop
  6e585c:      	nop

00000000006e5860 <search::AttributeVector::getUniqueValueCount() const (.localalias)>:
  6e5860:      	ldr	x2, [x0]
  6e5864:      	adrp	x1, 0x6e5000 <search::AttributeSaver::save(search::IAttributeSaveTarget&)+0x5c>
  6e5868:      	add	x1, x1, #0x56c
  6e586c:      	ldr	x2, [x2, #0x268]
  6e5870:      	cmp	x2, x1
  6e5874:      	b.ne	0x6e5888 <search::AttributeVector::getUniqueValueCount() const (.localalias)+0x28>
  6e5878:      	add	x0, x0, #0x110
  6e587c:      	ldr	x0, [x0]
  6e5880:      	and	x0, x0, #0xffffffff
  6e5884:      	ret
  6e5888:      	mov	x16, x2
  6e588c:      	br	x16

00000000006e5890 <search::(anonymous namespace)::ReadGuard::~ReadGuard()>:
  6e5890:      	adrp	x1, 0x103e000 <vtable for search::attribute::(anonymous namespace)::OperateOverDocIds<search::attribute::(anonymous namespace)::UpdateFast<search::IntegerAttributeTemplate<signed char>, search::attribute::(anonymous namespace)::Mul<long>>>+0x20>
  6e5894:      	stp	x29, x30, [sp, #-0x20]!
  6e5898:      	mov	x29, sp
  6e589c:      	add	x1, x1, #0x828
  6e58a0:      	add	x1, x1, #0x10
  6e58a4:      	str	x19, [sp, #0x10]
  6e58a8:      	mov	x19, x0
  6e58ac:      	str	x1, [x0], #0x18
  6e58b0:      	ldrb	w0, [x0, #0x8]
  6e58b4:      	tbnz	w0, #0x0, 0x6e58e4 <search::(anonymous namespace)::ReadGuard::~ReadGuard()+0x54>
  6e58b8:      	ldr	x0, [x19, #0x10]
  6e58bc:      	cbz	x0, 0x6e58c8 <search::(anonymous namespace)::ReadGuard::~ReadGuard()+0x38>
  6e58c0:      	mov	w1, #-0x2               // =-2
  6e58c4:      	ldaddl	w1, w1, [x0]
  6e58c8:      	mov	x0, x19
  6e58cc:      	bl	0x6687a0 <_ZN6search9attribute18AttributeReadGuardD2Ev@plt>
  6e58d0:      	mov	x0, x19
  6e58d4:      	ldr	x19, [sp, #0x10]
  6e58d8:      	mov	x1, #0x28               // =40
  6e58dc:      	ldp	x29, x30, [sp], #0x20
  6e58e0:      	b	0x66d3a0 <_ZdlPvm@plt>
  6e58e4:      	ldr	x0, [x19, #0x18]
  6e58e8:      	bl	0x675130 <pthread_rwlock_unlock@plt>
  6e58ec:      	b	0x6e58b8 <search::(anonymous namespace)::ReadGuard::~ReadGuard()+0x28>

00000000006e58f0 <search::(anonymous namespace)::exists(std::basic_string_view<char, std::char_traits<char>>)>:
  6e58f0:      	stp	x29, x30, [sp, #-0x70]!
  6e58f4:      	mov	x29, sp
  6e58f8:      	cmp	x1, #0x0
  6e58fc:      	ccmp	x0, #0x0, #0x4, eq
