0000000000785a80 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)>:
  785a80:	stp	x29, x30, [sp, #-240]!
  785a84:	mov	x29, sp
  785a88:	stp	x23, x24, [sp, #48]
  785a8c:	add	x23, sp, #0x70
  785a90:	mov	w24, w1
  785a94:	mov	w1, #0x5                   	// #5
  785a98:	stp	x19, x20, [sp, #16]
  785a9c:	mov	x19, x0
  785aa0:	mov	x0, x23
  785aa4:	stp	x21, x22, [sp, #32]
  785aa8:	mov	x22, x2
  785aac:	mov	w21, w3
  785ab0:	mov	w2, #0x0                   	// #0
  785ab4:	mov	w3, #0x0                   	// #0
  785ab8:	bl	66e690 <search::attribute::Config::Config(search::attribute::BasicType, search::attribute::CollectionType, bool)@plt>
  785abc:	mov	x0, #0x270                 	// #624
  785ac0:	bl	676a10 <operator new(unsigned long)@plt>
  785ac4:	adrp	x3, 10cf000 <vtable for vespalib::stash::DestructObject<search::attribute::RawMultiValueReadView<int> >@@Base+0x511c0>
  785ac8:	ldr	x3, [x3, #2168]
  785acc:	mov	x4, #0x100000001           	// #4294967297
  785ad0:	mov	x1, x22
  785ad4:	mov	x22, x0
  785ad8:	mov	x20, x0
  785adc:	str	x4, [x0, #8]
  785ae0:	mov	x2, x23
  785ae4:	add	x3, x3, #0x10
  785ae8:	str	x3, [x22], #16
  785aec:	mov	x0, x22
  785af0:	bl	66a7c0 <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char> >::SingleValueNumericAttribute(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, search::attribute::Config const&)@plt>
  785af4:	mov	w1, w24
  785af8:	add	x2, sp, #0x60
  785afc:	stp	x22, x20, [sp, #96]
  785b00:	mov	x0, x19
  785b04:	bl	6735a0 <search::SourceSelector::SourceSelector(unsigned char, std::shared_ptr<search::AttributeVector>)@plt>
  785b08:	ldr	x20, [sp, #104]
  785b0c:	cbz	x20, 785b34 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xb4>
  785b10:	add	x0, x20, #0x8
  785b14:	ldar	x2, [x0]
  785b18:	mov	x1, #0x100000001           	// #4294967297
  785b1c:	cmp	x2, x1
  785b20:	b.eq	785c28 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1a8>  // b.none
  785b24:	mov	w1, #0xffffffff            	// #-1
  785b28:	ldaddal	w1, w1, [x0]
  785b2c:	cmp	w1, #0x1
  785b30:	b.eq	785c74 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1f4>  // b.none
  785b34:	mov	x0, x23
  785b38:	bl	66d4e0 <search::attribute::Config::~Config()@plt>
  785b3c:	adrp	x0, 10cf000 <vtable for vespalib::stash::DestructObject<search::attribute::RawMultiValueReadView<int> >@@Base+0x511c0>
  785b40:	ldr	x0, [x0, #2288]
  785b44:	ldr	x20, [x19, #16]
  785b48:	add	x0, x0, #0x10
  785b4c:	str	x0, [x19]
  785b50:	str	x20, [x19, #32]
  785b54:	cmn	w21, #0x1
  785b58:	b.ne	785b70 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xf0>  // b.any
  785b5c:	ldp	x19, x20, [sp, #16]
  785b60:	ldp	x21, x22, [sp, #32]
  785b64:	ldp	x23, x24, [sp, #48]
  785b68:	ldp	x29, x30, [sp], #240
  785b6c:	ret
  785b70:	stp	x25, x26, [sp, #64]
  785b74:	add	x24, x20, #0x110
  785b78:	ldrb	w0, [x19, #12]
  785b7c:	mov	w25, w0
  785b80:	ldr	x0, [x24]
  785b84:	cbnz	w0, 785c80 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x200>
  785b88:	add	x0, x20, #0x188
  785b8c:	ldar	w0, [x0]
  785b90:	cbnz	w0, 785c80 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x200>
  785b94:	ldr	x23, [x20, #552]
  785b98:	add	w22, w21, #0x1
  785b9c:	add	x2, x20, #0x200
  785ba0:	add	x3, x20, #0x218
  785ba4:	cmp	x22, x23
  785ba8:	b.hi	785c50 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1d0>  // b.pmore
  785bac:	mov	x0, x3
  785bb0:	mov	x1, x22
  785bb4:	str	x2, [sp, #80]
  785bb8:	bl	6761c0 <vespalib::RcuVectorBase<signed char>::unsafe_resize(unsigned long)@plt>
  785bbc:	ldr	x2, [sp, #80]
  785bc0:	mov	w1, w25
  785bc4:	ldr	x0, [x2, #32]
  785bc8:	mov	x2, x22
  785bcc:	bl	660170 <memset@plt>
  785bd0:	dmb	ish
  785bd4:	str	x22, [x24]
  785bd8:	ldr	w0, [x20, #396]
  785bdc:	cmp	w21, w0
  785be0:	b.cc	785bec <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x16c>  // b.lo, b.ul, b.last
  785be4:	add	w0, w21, #0x1
  785be8:	str	w0, [x20, #396]
  785bec:	mov	x0, x20
  785bf0:	cmp	x22, x23
  785bf4:	b.ls	785c20 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1a0>  // b.plast
  785bf8:	bl	67b950 <search::AttributeVector::incGeneration()@plt>
  785bfc:	ldr	x0, [x19, #32]
  785c00:	mov	w1, #0x0                   	// #0
  785c04:	bl	66f0a0 <search::AttributeVector::commit(search::CommitParam::UpdateStats)@plt>
  785c08:	ldp	x25, x26, [sp, #64]
  785c0c:	ldp	x19, x20, [sp, #16]
  785c10:	ldp	x21, x22, [sp, #32]
  785c14:	ldp	x23, x24, [sp, #48]
  785c18:	ldp	x29, x30, [sp], #240
  785c1c:	ret
  785c20:	bl	66c7a0 <search::AttributeVector::reclaim_unused_memory()@plt>
  785c24:	b	785bfc <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x17c>
  785c28:	ldr	x1, [x20]
  785c2c:	mov	x0, x20
  785c30:	ldr	x1, [x1, #16]
  785c34:	str	xzr, [x20, #8]
  785c38:	blr	x1
  785c3c:	ldr	x1, [x20]
  785c40:	mov	x0, x20
  785c44:	ldr	x1, [x1, #24]
  785c48:	blr	x1
  785c4c:	b	785b34 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xb4>
  785c50:	mov	x1, x22
  785c54:	mov	x0, x3
  785c58:	stp	x3, x2, [sp, #80]
  785c5c:	bl	672210 <vespalib::RcuVectorBase<signed char>::calcNewSize(unsigned long) const@plt>
  785c60:	mov	x1, x0
  785c64:	ldr	x0, [sp, #80]
  785c68:	bl	661c50 <vespalib::RcuVectorBase<signed char>::expand(unsigned long)@plt>
  785c6c:	ldp	x3, x2, [sp, #80]
  785c70:	b	785bac <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x12c>
  785c74:	mov	x0, x20
  785c78:	bl	68b580 <std::_Sp_counted_base<(__gnu_cxx::_Lock_policy)2>::_M_release_last_use_cold()>
  785c7c:	b	785b34 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xb4>
  785c80:	adrp	x3, dba000 <_fini+0x25d10>
  785c84:	adrp	x1, dba000 <_fini+0x25d10>
  785c88:	adrp	x0, dba000 <_fini+0x25d10>
  785c8c:	add	x3, x3, #0x3b8
  785c90:	add	x1, x1, #0x4e8
  785c94:	add	x0, x0, #0x540
  785c98:	mov	w2, #0x53                  	// #83
  785c9c:	bl	6798c0 <__assert_fail@plt>
  785ca0:	adrp	x1, 10cb000 <vtable for search::SingleValueEnumAttribute<search::EnumAttribute<search::IntegerAttributeTemplate<short> > >@@Base+0x41870>
  785ca4:	ldr	x1, [x1, #1208]
  785ca8:	mov	x20, x0
  785cac:	ldr	x0, [x19, #24]
  785cb0:	add	x1, x1, #0x10
  785cb4:	str	x1, [x19]
  785cb8:	cbz	x0, 785cc0 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x240>
  785cbc:	bl	6b40a0 <std::_Sp_counted_base<(__gnu_cxx::_Lock_policy)2>::_M_release()>
  785cc0:	mov	x0, x20
  785cc4:	bl	671810 <_Unwind_Resume@plt>
  785cc8:	mov	x19, x0
  785ccc:	mov	x1, #0x270                 	// #624
  785cd0:	mov	x0, x20
  785cd4:	bl	66e720 <operator delete(void*, unsigned long)@plt>
  785cd8:	mov	x0, x23
  785cdc:	stp	x25, x26, [sp, #64]
  785ce0:	bl	66d4e0 <search::attribute::Config::~Config()@plt>
  785ce4:	mov	x0, x19
  785ce8:	bl	671810 <_Unwind_Resume@plt>
  785cec:	mov	x19, x0
  785cf0:	b	785cd8 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x258>
  785cf4:	ldr	x1, [sp, #104]
  785cf8:	mov	x19, x0
  785cfc:	cbz	x1, 785cd8 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x258>
  785d00:	mov	x0, x1
  785d04:	bl	6b40a0 <std::_Sp_counted_base<(__gnu_cxx::_Lock_policy)2>::_M_release()>
  785d08:	b	785cd8 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x258>

0000000000785d0c <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)>:
  785d0c:	stp	x29, x30, [sp, #-80]!
  785d10:	mov	x29, sp
  785d14:	stp	x19, x20, [sp, #16]
  785d18:	mov	x19, x8
  785d1c:	add	x8, sp, #0x48
  785d20:	mov	w20, w1
  785d24:	str	x21, [sp, #32]
  785d28:	mov	x21, x0
  785d2c:	bl	674540 <search::SourceSelector::extractLoadInfo(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)@plt>
  785d30:	ldr	x0, [sp, #72]
  785d34:	bl	67ab60 <search::SourceSelector::LoadInfo::load()@plt>
  785d38:	ldr	x2, [sp, #72]
  785d3c:	ldr	w0, [x2, #36]
  785d40:	sub	w20, w20, w0
  785d44:	cmp	w20, #0xfd
  785d48:	b.hi	785ecc <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1c0>  // b.pmore
  785d4c:	ldrb	w5, [x2, #32]
  785d50:	cmp	w20, w5
  785d54:	b.eq	785d78 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x6c>  // b.none
  785d58:	adrp	x0, 10e8000 <ns_log_logger+0x158>
  785d5c:	mov	w1, #0x2020                	// #8224
  785d60:	ldr	x3, [x0, #184]
  785d64:	movk	w1, #0x4e4f, lsl #16
  785d68:	add	x0, x0, #0xb8
  785d6c:	ldr	w3, [x3, #16]
  785d70:	cmp	w3, w1
  785d74:	b.eq	785e7c <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x170>  // b.none
  785d78:	mov	x0, #0x28                  	// #40
  785d7c:	str	x2, [sp, #56]
  785d80:	bl	676a10 <operator new(unsigned long)@plt>
  785d84:	ldr	x2, [sp, #56]
  785d88:	mov	w1, w20
  785d8c:	mov	w3, #0xffffffff            	// #-1
  785d90:	mov	x20, x0
  785d94:	bl	6757a0 <search::FixedSourceSelector::FixedSourceSelector(unsigned char, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)@plt>
  785d98:	ldr	x1, [sp, #72]
  785d9c:	ldr	x0, [x20, #32]
  785da0:	ldr	w1, [x1, #36]
  785da4:	str	x20, [x19]
  785da8:	str	w1, [x20, #8]
  785dac:	bl	660a10 <search::AttributeVector::load()@plt>
  785db0:	ldr	x0, [x19]
  785db4:	ldr	x6, [x0, #32]
  785db8:	ldrb	w4, [x0, #12]
  785dbc:	add	x0, x6, #0x188
  785dc0:	ldar	w3, [x0]
  785dc4:	cbz	w3, 785e38 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x12c>
  785dc8:	mov	x1, #0x0                   	// #0
  785dcc:	mov	w5, #0x0                   	// #0
  785dd0:	add	x0, x6, #0x240
  785dd4:	b	785de4 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xd8>
  785dd8:	add	x1, x1, #0x1
  785ddc:	cmp	x3, x1
  785de0:	b.eq	785e14 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x108>  // b.none
  785de4:	ldar	x2, [x0]
  785de8:	add	x2, x2, x1
  785dec:	ldrb	w2, [x2]
  785df0:	cmp	w4, w2, uxtb
  785df4:	b.cs	785dd8 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xcc>  // b.hs, b.nlast
  785df8:	ldr	x2, [x6, #544]
  785dfc:	add	w5, w5, #0x1
  785e00:	add	x2, x2, x1
  785e04:	strb	w4, [x2]
  785e08:	add	x1, x1, #0x1
  785e0c:	cmp	x3, x1
  785e10:	b.ne	785de4 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0xd8>  // b.any
  785e14:	cbz	w5, 785e38 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x12c>
  785e18:	adrp	x0, 10e8000 <ns_log_logger+0x158>
  785e1c:	mov	w1, #0x2020                	// #8224
  785e20:	ldr	x2, [x0, #184]
  785e24:	movk	w1, #0x4e4f, lsl #16
  785e28:	add	x0, x0, #0xb8
  785e2c:	ldr	w2, [x2, #8]
  785e30:	cmp	w2, w1
  785e34:	b.eq	785ea8 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x19c>  // b.none
  785e38:	ldr	x20, [sp, #72]
  785e3c:	cbz	x20, 785e68 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x15c>
  785e40:	mov	x1, x20
  785e44:	ldr	x0, [x1], #16
  785e48:	cmp	x0, x1
  785e4c:	b.eq	785e5c <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x150>  // b.none
  785e50:	ldr	x1, [x20, #16]
  785e54:	add	x1, x1, #0x1
  785e58:	bl	66e720 <operator delete(void*, unsigned long)@plt>
  785e5c:	mov	x0, x20
  785e60:	mov	x1, #0x30                  	// #48
  785e64:	bl	66e720 <operator delete(void*, unsigned long)@plt>
  785e68:	ldr	x21, [sp, #32]
  785e6c:	mov	x0, x19
  785e70:	ldp	x19, x20, [sp, #16]
  785e74:	ldp	x29, x30, [sp], #80
  785e78:	ret
  785e7c:	adrp	x4, dba000 <_fini+0x25d10>
  785e80:	adrp	x2, dba000 <_fini+0x25d10>
  785e84:	ldr	x7, [x21]
  785e88:	add	x4, x4, #0x658
  785e8c:	add	x2, x2, #0x5e8
  785e90:	mov	w6, w20
  785e94:	mov	w3, #0x63                  	// #99
  785e98:	mov	w1, #0x4                   	// #4
  785e9c:	bl	66f470 <ns_log::Logger::doLog(ns_log::Logger::LogLevel, char const*, int, char const*, ...)@plt>
  785ea0:	ldr	x2, [sp, #72]
  785ea4:	b	785d78 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x6c>
  785ea8:	adrp	x4, dba000 <_fini+0x25d10>
  785eac:	adrp	x2, dba000 <_fini+0x25d10>
  785eb0:	ldr	x6, [x21]
  785eb4:	add	x4, x4, #0x6a0
  785eb8:	add	x2, x2, #0x5e8
  785ebc:	mov	w3, #0x6e                  	// #110
  785ec0:	mov	w1, #0x2                   	// #2
  785ec4:	bl	66f470 <ns_log::Logger::doLog(ns_log::Logger::LogLevel, char const*, int, char const*, ...)@plt>
  785ec8:	b	785e38 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x12c>
  785ecc:	adrp	x3, dba000 <_fini+0x25d10>
  785ed0:	adrp	x1, dba000 <_fini+0x25d10>
  785ed4:	adrp	x0, dba000 <_fini+0x25d10>
  785ed8:	add	x3, x3, #0x580
  785edc:	add	x1, x1, #0x5e8
  785ee0:	add	x0, x0, #0x638
  785ee4:	mov	w2, #0x61                  	// #97
  785ee8:	bl	6798c0 <__assert_fail@plt>
  785eec:	mov	x19, x0
  785ef0:	mov	x1, #0x28                  	// #40
  785ef4:	mov	x0, x20
  785ef8:	bl	66e720 <operator delete(void*, unsigned long)@plt>
  785efc:	ldr	x20, [sp, #72]
  785f00:	cbz	x20, 785f2c <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x220>
  785f04:	mov	x1, x20
  785f08:	ldr	x0, [x1], #16
  785f0c:	cmp	x0, x1
  785f10:	b.eq	785f20 <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x214>  // b.none
  785f14:	ldr	x1, [x20, #16]
  785f18:	add	x1, x1, #0x1
  785f1c:	bl	66e720 <operator delete(void*, unsigned long)@plt>
  785f20:	mov	x0, x20
  785f24:	mov	x1, #0x30                  	// #48
  785f28:	bl	66e720 <operator delete(void*, unsigned long)@plt>
  785f2c:	mov	x0, x19
  785f30:	bl	671810 <_Unwind_Resume@plt>
  785f34:	mov	x19, x0
  785f38:	b	785efc <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1f0>
  785f3c:	ldr	x1, [x19]
  785f40:	mov	x19, x0
  785f44:	cbz	x1, 785efc <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1f0>
  785f48:	ldr	x2, [x1]
  785f4c:	mov	x0, x1
  785f50:	ldr	x1, [x2, #40]
  785f54:	blr	x1
  785f58:	b	785efc <search::FixedSourceSelector::load(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned int)+0x1f0>
  785f5c:	nop
