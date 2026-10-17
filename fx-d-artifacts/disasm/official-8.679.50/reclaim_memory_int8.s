  94b8a0:      	ldp	x2, x3, [x0, #0xf0]
  94b8a4:      	cmp	x2, x3
  94b8a8:      	b.eq	0x94b8b8 <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char>>::reclaim_memory(vespalib::Generation)+0x18>
  94b8ac:      	ldr	x2, [x2, #0x8]
  94b8b0:      	cmp	x1, x2
  94b8b4:      	b.hi	0x94b8bc <search::SingleValueNumericAttribute<search::IntegerAttributeTemplate<signed char>>::reclaim_memory(vespalib::Generation)+0x1c>
  94b8b8:      	ret
  94b8bc:      	add	x0, x0, #0xd8
  94b8c0:      	mov	w2, #0x0                // =0
  94b8c4:      	b	0x6632e0 <_ZN8vespalib18GenerationHoldListISt10unique_ptrINS_18GenerationHeldBaseESt14default_deleteIS2_EELb1ELb0EE16reclaim_internalINS6_8NoopFuncEEEvNS_10GenerationET_@plt>
  94b8c8:      	nop
  94b8cc:      	nop
  94b8d0:      	nop
  94b8d4:      	nop
  94b8d8:      	nop
  94b8dc:      	nop
