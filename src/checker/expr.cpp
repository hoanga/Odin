void     check_expr                (Checker *c, Operand *operand, AstNode *expression);
void     check_multi_expr          (Checker *c, Operand *operand, AstNode *expression);
void     check_expr_or_type        (Checker *c, Operand *operand, AstNode *expression);
ExprKind check_expr_base           (Checker *c, Operand *operand, AstNode *expression, Type *type_hint = NULL);
Type *   check_type                (Checker *c, AstNode *expression, Type *named_type = NULL, CycleChecker *cycle_checker = NULL);
void     check_type_decl           (Checker *c, Entity *e, AstNode *type_expr, Type *def, CycleChecker *cycle_checker);
Entity * check_selector            (Checker *c, Operand *operand, AstNode *node);
void     check_not_tuple           (Checker *c, Operand *operand);
b32      check_value_is_expressible(Checker *c, ExactValue in_value, Type *type, ExactValue *out_value);
void     convert_to_typed          (Checker *c, Operand *operand, Type *target_type, i32 level = 0);
gbString expr_to_string            (AstNode *expression);
void     check_entity_decl         (Checker *c, Entity *e, DeclInfo *decl, Type *named_type, CycleChecker *cycle_checker = NULL);
void     check_proc_body           (Checker *c, Token token, DeclInfo *decl, Type *type, AstNode *body);
void     update_expr_type          (Checker *c, AstNode *e, Type *type, b32 final);



b32 check_is_assignable_to_using_subtype(Type *dst, Type *src) {
	Type *prev_src = src;
	// Type *prev_dst = dst;
	src = base_type(type_deref(src));
	// dst = base_type(type_deref(dst));
	b32 src_is_ptr = src != prev_src;
	// b32 dst_is_ptr = dst != prev_dst;

	if (is_type_struct(src)) {
		for (isize i = 0; i < src->Record.field_count; i++) {
			Entity *f = src->Record.fields[i];
			if (f->kind == Entity_Variable && f->Variable.anonymous) {
				if (are_types_identical(dst, f->type)) {
					return true;
				}
				if (src_is_ptr && is_type_pointer(dst)) {
					if (are_types_identical(type_deref(dst), f->type)) {
						return true;
					}
				}
				b32 ok = check_is_assignable_to_using_subtype(dst, f->type);
				if (ok) {
					return true;
				}
			}
		}
	}
	return false;
}


b32 check_is_assignable_to(Checker *c, Operand *operand, Type *type, b32 is_argument = false) {
	PROF_PROC();

	if (operand->mode == Addressing_Invalid ||
	    type == t_invalid) {
		return true;
	}

	if (operand->mode == Addressing_Builtin) {
		return false;
	}

	Type *s = operand->type;

	if (are_types_identical(s, type)) {
		return true;
	}

	Type *src = base_type(s);
	Type *dst = base_type(type);

	if (is_type_untyped(src)) {
		switch (dst->kind) {
		case Type_Basic:
			if (operand->mode == Addressing_Constant) {
				return check_value_is_expressible(c, operand->value, dst, NULL);
			}
			if (src->kind == Type_Basic && src->Basic.kind == Basic_UntypedBool) {
				return is_type_boolean(dst);
			}
			break;
		}
		if (type_has_nil(dst)) {
			return operand->mode == Addressing_Value && operand->type == t_untyped_nil;
		}
	}

	if (are_types_identical(dst, src) && (!is_type_named(dst) || !is_type_named(src))) {
		if (is_type_enum(dst) && is_type_enum(src))  {
			return are_types_identical(s, type);
		}
		return true;
	}

	if (is_type_maybe(dst)) {
		Type *elem = base_type(dst)->Maybe.elem;
		return are_types_identical(elem, src);
	}

	if (is_type_untyped_nil(src)) {
		return type_has_nil(dst);
	}

	// ^T <- rawptr
	// TODO(bill): Should C-style (not C++) pointer cast be allowed?
	// if (is_type_pointer(dst) && is_type_rawptr(src)) {
	    // return true;
	// }

	// rawptr <- ^T
	if (is_type_rawptr(dst) && is_type_pointer(src)) {
	    return true;
	}



	if (dst->kind == Type_Array && src->kind == Type_Array) {
		if (are_types_identical(dst->Array.elem, src->Array.elem)) {
			return dst->Array.count == src->Array.count;
		}
	}

	if (dst->kind == Type_Slice && src->kind == Type_Slice) {
		if (are_types_identical(dst->Slice.elem, src->Slice.elem)) {
			return true;
		}
	}

	if (is_type_union(dst)) {
		for (isize i = 0; i < dst->Record.field_count; i++) {
			Entity *f = dst->Record.fields[i];
			if (are_types_identical(f->type, s)) {
				return true;
			}
		}
	}


	if (dst == t_any) {
		// NOTE(bill): Anything can cast to `Any`
		add_type_info_type(c, s);
		return true;
	}

	if (true || is_argument) {
		// NOTE(bill): Polymorphism for subtyping
		if (check_is_assignable_to_using_subtype(type, src)) {
			return true;
		}
	}

	return false;
}


// NOTE(bill): `content_name` is for debugging and error messages
void check_assignment(Checker *c, Operand *operand, Type *type, String context_name, b32 is_argument = false) {
	PROF_PROC();

	check_not_tuple(c, operand);
	if (operand->mode == Addressing_Invalid) {
		return;
	}

	if (is_type_untyped(operand->type)) {
		Type *target_type = type;

		if (type == NULL || is_type_any(type) || is_type_untyped_nil(type)) {
			if (type == NULL && base_type(operand->type) == t_untyped_nil) {
				error(ast_node_token(operand->expr), "Use of untyped nil in %.*s", LIT(context_name));
				operand->mode = Addressing_Invalid;
				return;
			}

			add_type_info_type(c, type);
			target_type = default_type(operand->type);
		}
		convert_to_typed(c, operand, target_type);
		if (operand->mode == Addressing_Invalid) {
			return;
		}
	}

	if (type != NULL) {
		if (!check_is_assignable_to(c, operand, type, is_argument)) {
			gbString type_string = type_to_string(type);
			gbString op_type_string = type_to_string(operand->type);
			gbString expr_str = expr_to_string(operand->expr);
			defer (gb_string_free(type_string));
			defer (gb_string_free(op_type_string));
			defer (gb_string_free(expr_str));

			if (operand->mode == Addressing_Builtin) {
				// TODO(bill): is this a good enough error message?
				error(ast_node_token(operand->expr),
				      "Cannot assign builtin procedure `%s` in %.*s",
				      expr_str,
				      LIT(context_name));
			} else {
				// TODO(bill): is this a good enough error message?
				error(ast_node_token(operand->expr),
				      "Cannot assign value `%s` of type `%s` to `%s` in %.*s",
				      expr_str,
				      op_type_string,
				      type_string,
				      LIT(context_name));
			}
			operand->mode = Addressing_Invalid;
			return;
		}
	}
}


void populate_using_entity_map(Checker *c, AstNode *node, Type *t, Map<Entity *> *entity_map) {
	PROF_PROC();

	t = base_type(type_deref(t));
	gbString str = expr_to_string(node);
	defer (gb_string_free(str));

	if (t->kind == Type_Record) {
		for (isize i = 0; i < t->Record.field_count; i++) {
			Entity *f = t->Record.fields[i];
			GB_ASSERT(f->kind == Entity_Variable);
			String name = f->token.string;
			HashKey key = hash_string(name);
			Entity **found = map_get(entity_map, key);
			if (found != NULL) {
				Entity *e = *found;
				// TODO(bill): Better type error
				error(e->token, "`%.*s` is already declared in `%s`", LIT(name), str);
			} else {
				map_set(entity_map, key, f);
				add_entity(c, c->context.scope, NULL, f);
				if (f->Variable.anonymous) {
					populate_using_entity_map(c, node, f->type, entity_map);
				}
			}
		}
	}

}

void check_const_decl(Checker *c, Entity *e, AstNode *type_expr, AstNode *init_expr);

void check_fields(Checker *c, AstNode *node, AstNodeArray decls,
                  Entity **fields, isize field_count,
                  Entity **other_fields, isize other_field_count,
                  CycleChecker *cycle_checker, String context) {
	PROF_PROC();

	Map<Entity *> entity_map = {};
	map_init(&entity_map, heap_allocator());
	defer (map_destroy(&entity_map));

	isize other_field_index = 0;
	Entity *using_index_expr = NULL;


	gbTempArenaMemory tmp = gb_temp_arena_memory_begin(&c->tmp_arena);
	defer (gb_temp_arena_memory_end(tmp));
	struct Delay {
		Entity *e;
		AstNode *t;
	};
	Array<Delay> delayed_const; array_init(&delayed_const, c->tmp_allocator, other_field_count);
	Array<Delay> delayed_type;  array_init(&delayed_type,  c->tmp_allocator, other_field_count);

	for_array(decl_index, decls) {
		AstNode *decl = decls[decl_index];
		if (decl->kind == AstNode_ConstDecl) {
			ast_node(cd, ConstDecl, decl);

			isize entity_count = cd->names.count;
			isize entity_index = 0;
			Entity **entities = gb_alloc_array(c->allocator, Entity *, entity_count);

			for_array(i, cd->values) {
				AstNode *name = cd->names[i];
				AstNode *value = cd->values[i];

				GB_ASSERT(name->kind == AstNode_Ident);
				ExactValue v = {ExactValue_Invalid};
				Token name_token = name->Ident;
				Entity *e = make_entity_constant(c->allocator, c->context.scope, name_token, NULL, v);
				entities[entity_index++] = e;

				Delay delay = {e, cd->type};
				array_add(&delayed_const, delay);
			}

			isize lhs_count = cd->names.count;
			isize rhs_count = cd->values.count;

			// TODO(bill): Better error messages or is this good enough?
			if (rhs_count == 0 && cd->type == NULL) {
				error(ast_node_token(node), "Missing type or initial expression");
			} else if (lhs_count < rhs_count) {
				error(ast_node_token(node), "Extra initial expression");
			}

			for_array(i, cd->names) {
				AstNode *name = cd->names[i];
				Entity *e = entities[i];
				Token name_token = name->Ident;
				if (name_token.string == "_") {
					other_fields[other_field_index++] = e;
				} else {
					HashKey key = hash_string(name_token.string);
					if (map_get(&entity_map, key) != NULL) {
						// TODO(bill): Scope checking already checks the declaration
						error(name_token, "`%.*s` is already declared in this structure", LIT(name_token.string));
					} else {
						map_set(&entity_map, key, e);
						other_fields[other_field_index++] = e;
					}
					add_entity(c, c->context.scope, name, e);
				}
			}
		} else if (decl->kind == AstNode_TypeDecl) {
			ast_node(td, TypeDecl, decl);
			Token name_token = td->name->Ident;

			Entity *e = make_entity_type_name(c->allocator, c->context.scope, name_token, NULL);
			Delay delay = {e, td->type};
			array_add(&delayed_type, delay);

			if (name_token.string == "_") {
				other_fields[other_field_index++] = e;
			} else {
				HashKey key = hash_string(name_token.string);
				if (map_get(&entity_map, key) != NULL) {
					// TODO(bill): Scope checking already checks the declaration
					error(name_token, "`%.*s` is already declared in this structure", LIT(name_token.string));
				} else {
					map_set(&entity_map, key, e);
					other_fields[other_field_index++] = e;
				}
				add_entity(c, c->context.scope, td->name, e);
				add_entity_use(c, td->name, e);
			}
		}
	}

	for_array(i, delayed_type) {
		check_const_decl(c, delayed_type[i].e, delayed_type[i].t, NULL);
	}
	for_array(i, delayed_const) {
		check_type_decl(c, delayed_const[i].e, delayed_const[i].t, NULL, NULL);
	}

	if (node->kind == AstNode_UnionType) {
		isize field_index = 0;
		fields[field_index++] = make_entity_type_name(c->allocator, c->context.scope, empty_token, NULL);
		for_array(decl_index, decls) {
			AstNode *decl = decls[decl_index];
			if (decl->kind != AstNode_VarDecl) {
				continue;
			}

			ast_node(vd, VarDecl, decl);
			Type *base_type = check_type(c, vd->type, NULL, cycle_checker);

			for_array(name_index, vd->names) {
				AstNode *name = vd->names[name_index];
				Token name_token = name->Ident;

				Type *type = make_type_named(c->allocator, name_token.string, base_type, NULL);
				Entity *e = make_entity_type_name(c->allocator, c->context.scope, name_token, type);
				type->Named.type_name = e;
				add_entity(c, c->context.scope, name, e);

				if (name_token.string == "_") {
					error(name_token, "`_` cannot be used a union subtype");
					continue;
				}

				HashKey key = hash_string(name_token.string);
				if (map_get(&entity_map, key) != NULL) {
					// TODO(bill): Scope checking already checks the declaration
					error(name_token, "`%.*s` is already declared in this union", LIT(name_token.string));
				} else {
					map_set(&entity_map, key, e);
					fields[field_index++] = e;
				}
				add_entity_use(c, name, e);
			}
		}
	} else {
		isize field_index = 0;
		for_array(decl_index, decls) {
			AstNode *decl = decls[decl_index];
			if (decl->kind != AstNode_VarDecl) {
				continue;
			}
			ast_node(vd, VarDecl, decl);

			Type *type = check_type(c, vd->type, NULL, cycle_checker);

			if (vd->is_using) {
				if (vd->names.count > 1) {
					error(ast_node_token(vd->names[0]),
					      "Cannot apply `using` to more than one of the same type");
				}
			}

			for_array(name_index, vd->names) {
				AstNode *name = vd->names[name_index];
				Token name_token = name->Ident;

				Entity *e = make_entity_field(c->allocator, c->context.scope, name_token, type, vd->is_using, cast(i32)field_index);
				e->identifier = name;
				if (name_token.string == "_") {
					fields[field_index++] = e;
				} else {
					HashKey key = hash_string(name_token.string);
					if (map_get(&entity_map, key) != NULL) {
						// TODO(bill): Scope checking already checks the declaration
						error(name_token, "`%.*s` is already declared in this type", LIT(name_token.string));
					} else {
						map_set(&entity_map, key, e);
						fields[field_index++] = e;
						add_entity(c, c->context.scope, name, e);
					}
					add_entity_use(c, name, e);
				}
			}


			if (vd->is_using) {
				Type *t = base_type(type_deref(type));
				if (!is_type_struct(t) && !is_type_raw_union(t)) {
					Token name_token = vd->names[0]->Ident;
					if (is_type_indexable(t)) {
						b32 ok = true;
						for_array(emi, entity_map.entries) {
							Entity *e = entity_map.entries[emi].value;
							if (e->kind == Entity_Variable && e->Variable.anonymous) {
								if (is_type_indexable(e->type)) {
									if (e->identifier != vd->names[0]) {
										ok = false;
										using_index_expr = e;
										break;
									}
								}
							}
						}
						if (ok) {
							using_index_expr = fields[field_index-1];
						} else {
							fields[field_index-1]->Variable.anonymous = false;
							error(name_token, "Previous `using` for an index expression `%.*s`", LIT(name_token.string));
						}
					} else {
						error(name_token, "`using` on a field `%.*s` must be a `struct` or `raw_union`", LIT(name_token.string));
						continue;
					}
				}

				populate_using_entity_map(c, node, type, &entity_map);
			}
		}
	}
}


// TODO(bill): Cleanup struct field reordering
// TODO(bill): Inline sorting procedure?
gb_global BaseTypeSizes __checker_sizes = {};
gb_global gbAllocator   __checker_allocator = {};

GB_COMPARE_PROC(cmp_struct_entity_size) {
	// Rule:
	// Biggest to smallest alignment
	// if same alignment: biggest to smallest size
	// if same size: order by source order
	Entity *x = *(Entity **)a;
	Entity *y = *(Entity **)b;
	GB_ASSERT(x != NULL);
	GB_ASSERT(y != NULL);
	GB_ASSERT(x->kind == Entity_Variable);
	GB_ASSERT(y->kind == Entity_Variable);
	i64 xa = type_align_of(__checker_sizes, __checker_allocator, x->type);
	i64 ya = type_align_of(__checker_sizes, __checker_allocator, y->type);
	i64 xs = type_size_of(__checker_sizes, __checker_allocator, x->type);
	i64 ys = type_size_of(__checker_sizes, __checker_allocator, y->type);

	if (xa == ya) {
		if (xs == ys) {
			i32 diff = x->Variable.field_index - y->Variable.field_index;
			return diff < 0 ? -1 : diff > 0;
		}
		return xs > ys ? -1 : xs < ys;
	}
	return xa > ya ? -1 : xa < ya;
}

void check_struct_type(Checker *c, Type *struct_type, AstNode *node, CycleChecker *cycle_checker) {
	PROF_PROC();

	GB_ASSERT(is_type_struct(struct_type));
	ast_node(st, StructType, node);

	isize field_count = 0;
	isize other_field_count = 0;
	for_array(decl_index, st->decls) {
		AstNode *decl = st->decls[decl_index];
		switch (decl->kind) {
		case_ast_node(vd, VarDecl, decl);
			field_count += vd->names.count;
		case_end;

		case_ast_node(cd, ConstDecl, decl);
			other_field_count += cd->names.count;
		case_end;

		case_ast_node(td, TypeDecl, decl);
			other_field_count += 1;
		case_end;
		}
	}

	Entity **fields = gb_alloc_array(c->allocator, Entity *, field_count);
	Entity **other_fields = gb_alloc_array(c->allocator, Entity *, other_field_count);

	check_fields(c, node, st->decls, fields, field_count, other_fields, other_field_count, cycle_checker, make_string("struct"));


	struct_type->Record.struct_is_packed    = st->is_packed;
	struct_type->Record.struct_is_ordered   = st->is_ordered;
	struct_type->Record.fields              = fields;
	struct_type->Record.fields_in_src_order = fields;
	struct_type->Record.field_count         = field_count;
	struct_type->Record.other_fields        = other_fields;
	struct_type->Record.other_field_count   = other_field_count;



	if (!st->is_packed && !st->is_ordered) {
		// NOTE(bill): Reorder fields for reduced size/performance

		Entity **reordered_fields = gb_alloc_array(c->allocator, Entity *, field_count);
		for (isize i = 0; i < field_count; i++) {
			reordered_fields[i] = struct_type->Record.fields_in_src_order[i];
		}

		// NOTE(bill): Hacky thing
		// TODO(bill): Probably make an inline sorting procedure rather than use global variables
		__checker_sizes = c->sizes;
		__checker_allocator = c->allocator;
		// NOTE(bill): compound literal order must match source not layout
		gb_sort_array(reordered_fields, field_count, cmp_struct_entity_size);

		for (isize i = 0; i < field_count; i++) {
			reordered_fields[i]->Variable.field_index = i;
		}

		struct_type->Record.fields = reordered_fields;
	}

	type_set_offsets(c->sizes, c->allocator, struct_type);
}

void check_union_type(Checker *c, Type *union_type, AstNode *node, CycleChecker *cycle_checker) {
	PROF_PROC();

	GB_ASSERT(is_type_union(union_type));
	ast_node(ut, UnionType, node);

	isize field_count = 1;
	isize other_field_count = 0;
	for_array(decl_index, ut->decls) {
		AstNode *decl = ut->decls[decl_index];
		switch (decl->kind) {
		case_ast_node(vd, VarDecl, decl);
			field_count += vd->names.count;
		case_end;

		case_ast_node(cd, ConstDecl, decl);
			other_field_count += cd->names.count;
		case_end;

		case_ast_node(td, TypeDecl, decl);
			other_field_count += 1;
		case_end;
		}
	}

	Entity **fields = gb_alloc_array(c->allocator, Entity *, field_count);
	Entity **other_fields = gb_alloc_array(c->allocator, Entity *, other_field_count);

	check_fields(c, node, ut->decls, fields, field_count, other_fields, other_field_count, cycle_checker, make_string("union"));

	union_type->Record.fields            = fields;
	union_type->Record.field_count       = field_count;
	union_type->Record.other_fields      = other_fields;
	union_type->Record.other_field_count = other_field_count;
}

void check_raw_union_type(Checker *c, Type *union_type, AstNode *node, CycleChecker *cycle_checker) {
	PROF_PROC();

	GB_ASSERT(node->kind == AstNode_RawUnionType);
	GB_ASSERT(is_type_raw_union(union_type));
	ast_node(ut, RawUnionType, node);

	isize field_count = 0;
	isize other_field_count = 0;
	for_array(decl_index, ut->decls) {
		AstNode *decl = ut->decls[decl_index];
		switch (decl->kind) {
		case_ast_node(vd, VarDecl, decl);
			field_count += vd->names.count;
		case_end;

		case_ast_node(cd, ConstDecl, decl);
			other_field_count += cd->names.count;
		case_end;

		case_ast_node(td, TypeDecl, decl);
			other_field_count += 1;
		case_end;
		}
	}

	Entity **fields = gb_alloc_array(c->allocator, Entity *, field_count);
	Entity **other_fields = gb_alloc_array(c->allocator, Entity *, other_field_count);

	check_fields(c, node, ut->decls, fields, field_count, other_fields, other_field_count, cycle_checker, make_string("raw union"));

	union_type->Record.fields = fields;
	union_type->Record.field_count = field_count;
	union_type->Record.other_fields = other_fields;
	union_type->Record.other_field_count = other_field_count;
}

GB_COMPARE_PROC(cmp_enum_order) {
	// Rule:
	// Biggest to smallest alignment
	// if same alignment: biggest to smallest size
	// if same size: order by source order
	Entity *x = *(Entity **)a;
	Entity *y = *(Entity **)b;
	GB_ASSERT(x != NULL);
	GB_ASSERT(y != NULL);
	GB_ASSERT(x->kind == Entity_Constant);
	GB_ASSERT(y->kind == Entity_Constant);
	GB_ASSERT(x->Constant.value.kind == ExactValue_Integer);
	GB_ASSERT(y->Constant.value.kind == ExactValue_Integer);
	i64 i = x->Constant.value.value_integer;
	i64 j = y->Constant.value.value_integer;

	return i < j ? -1 : i > j;
}



void check_enum_type(Checker *c, Type *enum_type, Type *named_type, AstNode *node) {
	PROF_PROC();

	GB_ASSERT(node->kind == AstNode_EnumType);
	GB_ASSERT(is_type_enum(enum_type));
	ast_node(et, EnumType, node);

	Map<Entity *> entity_map = {};
	map_init(&entity_map, heap_allocator());
	defer (map_destroy(&entity_map));

	Type *base_type = t_int;
	if (et->base_type != NULL) {
		base_type = check_type(c, et->base_type);
	}

	if (base_type == NULL || !is_type_integer(base_type)) {
		error(et->token, "Base type for enumeration must be an integer");
		return;
	} else
	if (base_type == NULL) {
		base_type = t_int;
	}
	enum_type->Record.enum_base = base_type;

	Entity **fields = gb_alloc_array(c->allocator, Entity *, et->fields.count);
	isize field_index = 0;
	ExactValue iota = make_exact_value_integer(-1);
	i64 min_value = 0;
	i64 max_value = 0;

	Type *constant_type = enum_type;
	if (named_type != NULL) {
		constant_type = named_type;
	}
	Entity *blank_entity = make_entity_constant(c->allocator, c->context.scope, blank_token, constant_type, make_exact_value_integer(0));;

	for_array(i, et->fields) {
		AstNode *field = et->fields[i];

		ast_node(f, FieldValue, field);
		Token name_token = f->field->Ident;

		if (name_token.string == "count") {
			error(name_token, "`count` is a reserved identifier for enumerations");
			fields[field_index++] = blank_entity;
			continue;
		} else if (name_token.string == "min_value") {
			error(name_token, "`min_value` is a reserved identifier for enumerations");
			fields[field_index++] = blank_entity;
			continue;
		} else if (name_token.string == "max_value") {
			error(name_token, "`max_value` is a reserved identifier for enumerations");
			fields[field_index++] = blank_entity;
			continue;
		}

		Operand o = {};
		if (f->value != NULL) {
			check_expr(c, &o, f->value);
			if (o.mode != Addressing_Constant) {
				error(ast_node_token(f->value), "Enumeration value must be a constant integer");
				o.mode = Addressing_Invalid;
			}
			if (o.mode != Addressing_Invalid) {
				check_assignment(c, &o, constant_type, make_string("enumeration"));
			}
			if (o.mode != Addressing_Invalid) {
				iota = o.value;
			} else {
				Token add_token = {Token_Add};
				iota = exact_binary_operator_value(add_token, iota, make_exact_value_integer(1));
			}
		} else {
			Token add_token = {Token_Add};
			iota = exact_binary_operator_value(add_token, iota, make_exact_value_integer(1));
		}


		Entity *e = make_entity_constant(c->allocator, c->context.scope, name_token, constant_type, iota);
		if (min_value > iota.value_integer) {
			min_value = iota.value_integer;
		}
		if (max_value < iota.value_integer) {
			max_value = iota.value_integer;
		}

		HashKey key = hash_string(name_token.string);
		if (map_get(&entity_map, key)) {
			// TODO(bill): Scope checking already checks the declaration
			error(name_token, "`%.*s` is already declared in this enumeration", LIT(name_token.string));
		} else {
			map_set(&entity_map, key, e);
			add_entity(c, c->context.scope, NULL, e);
			fields[field_index++] = e;
		}
		add_entity_use(c, f->field, e);
	}

	GB_ASSERT(field_index <= et->fields.count);

	gb_sort_array(fields, field_index, cmp_enum_order);

	enum_type->Record.other_fields = fields;
	enum_type->Record.other_field_count = field_index;

	enum_type->Record.enum_count = make_entity_constant(c->allocator, NULL,
		make_token_ident(make_string("count")), t_int, make_exact_value_integer(enum_type->Record.other_field_count));
	enum_type->Record.min_value  = make_entity_constant(c->allocator, NULL,
		make_token_ident(make_string("min_value")), constant_type, make_exact_value_integer(min_value));
	enum_type->Record.max_value  = make_entity_constant(c->allocator, NULL,
		make_token_ident(make_string("max_value")), constant_type, make_exact_value_integer(max_value));
}

Type *check_get_params(Checker *c, Scope *scope, AstNodeArray params, b32 *is_variadic_) {
	PROF_PROC();

	if (params.count == 0) {
		return NULL;
	}

	b32 is_variadic = false;

	Type *tuple = make_type_tuple(c->allocator);

	isize variable_count = 0;
	for_array(i, params) {
		AstNode *field = params[i];
		ast_node(p, Parameter, field);
		variable_count += p->names.count;
	}

	Entity **variables = gb_alloc_array(c->allocator, Entity *, variable_count);
	isize variable_index = 0;
	for_array(i, params) {
		ast_node(p, Parameter, params[i]);
		AstNode *type_expr = p->type;
		if (type_expr) {
			if (type_expr->kind == AstNode_Ellipsis) {
				type_expr = type_expr->Ellipsis.expr;
				if (i+1 == params.count) {
					is_variadic = true;
				} else {
					error(ast_node_token(params[i]), "Invalid AST: Invalid variadic parameter");
				}
			}

			Type *type = check_type(c, type_expr);
			for_array(j, p->names) {
				AstNode *name = p->names[j];
				if (name->kind == AstNode_Ident) {
					Entity *param = make_entity_param(c->allocator, scope, name->Ident, type, p->is_using);
					add_entity(c, scope, name, param);
					variables[variable_index++] = param;
				} else {
					error(ast_node_token(name), "Invalid AST: Invalid parameter");
				}
			}
		}
	}

	if (is_variadic && params.count > 0) {
		// NOTE(bill): Change last variadic parameter to be a slice
		// Custom Calling convention for variadic parameters
		Entity *end = variables[params.count-1];
		end->type = make_type_slice(c->allocator, end->type);
	}

	tuple->Tuple.variables = variables;
	tuple->Tuple.variable_count = variable_count;

	if (is_variadic_) *is_variadic_ = is_variadic;

	return tuple;
}

Type *check_get_results(Checker *c, Scope *scope, AstNodeArray results) {
	PROF_PROC();

	if (results.count == 0) {
		return NULL;
	}
	Type *tuple = make_type_tuple(c->allocator);

	Entity **variables = gb_alloc_array(c->allocator, Entity *, results.count);
	isize variable_index = 0;
	for_array(i, results) {
		AstNode *item = results[i];
		Type *type = check_type(c, item);
		Token token = ast_node_token(item);
		token.string = make_string(""); // NOTE(bill): results are not named
		// TODO(bill): Should I have named results?
		Entity *param = make_entity_param(c->allocator, scope, token, type, false);
		// NOTE(bill): No need to record
		variables[variable_index++] = param;
	}
	tuple->Tuple.variables = variables;
	tuple->Tuple.variable_count = results.count;

	return tuple;
}


void check_procedure_type(Checker *c, Type *type, AstNode *proc_type_node) {
	PROF_PROC();

	ast_node(pt, ProcType, proc_type_node);

	b32 variadic = false;
	Type *params  = check_get_params(c, c->context.scope, pt->params, &variadic);
	Type *results = check_get_results(c, c->context.scope, pt->results);

	isize param_count = 0;
	isize result_count = 0;
	if (params)  param_count  = params ->Tuple.variable_count;
	if (results) result_count = results->Tuple.variable_count;


	type->Proc.scope            = c->context.scope;
	type->Proc.params           = params;
	type->Proc.param_count      = param_count;
	type->Proc.results          = results;
	type->Proc.result_count     = result_count;
	type->Proc.variadic         = variadic;
	// type->Proc.implicit_context = implicit_context;
}


void check_identifier(Checker *c, Operand *o, AstNode *n, Type *named_type, CycleChecker *cycle_checker) {
	PROF_PROC();

	GB_ASSERT(n->kind == AstNode_Ident);
	o->mode = Addressing_Invalid;
	o->expr = n;
	Entity *e = scope_lookup_entity(c->context.scope, n->Ident.string);
	if (e == NULL) {
		if (n->Ident.string == "_") {
			error(n->Ident, "`_` cannot be used as a value type");
		} else {
			error(n->Ident, "Undeclared name: %.*s", LIT(n->Ident.string));
		}
		o->type = t_invalid;
		o->mode = Addressing_Invalid;
		if (named_type != NULL) {
			set_base_type(named_type, t_invalid);
		}
		return;
	}
	add_entity_use(c, n, e);

	// CycleChecker local_cycle_checker = {};
	// if (cycle_checker == NULL) {
	// 	cycle_checker = &local_cycle_checker;
	// }
	// defer (cycle_checker_destroy(&local_cycle_checker));

	check_entity_decl(c, e, NULL, named_type, cycle_checker);

	if (e->type == NULL) {
		compiler_error("Compiler error: How did this happen? type: %s; identifier: %.*s\n", type_to_string(e->type), LIT(n->Ident.string));
		return;
	}

	Type *type = e->type;

	switch (e->kind) {
	case Entity_Constant:
		if (type == t_invalid) {
			o->type = t_invalid;
			return;
		}
		o->value = e->Constant.value;
		GB_ASSERT(o->value.kind != ExactValue_Invalid);
		o->mode = Addressing_Constant;
		break;

	case Entity_Variable:
		e->Variable.used = true;
		if (type == t_invalid) {
			o->type = t_invalid;
			return;
		}
	#if 0
		if (e->Variable.param) {
			o->mode = Addressing_Value;
		} else {
			o->mode = Addressing_Variable;
		}
	#else
		o->mode = Addressing_Variable;
	#endif
		break;

	case Entity_TypeName: {
		o->mode = Addressing_Type;
#if 0
	// TODO(bill): Fix cyclical dependancy checker
		if (cycle_checker != NULL) {
			for_array(i, cycle_checker->path) {
				Entity *prev = cycle_checker->path[i];
				if (prev == e) {
					error(e->token, "Illegal declaration cycle for %.*s", LIT(e->token.string));
					for (isize j = i; j < gb_array_count(cycle_checker->path); j++) {
						Entity *ref = cycle_checker->path[j];
						error(ref->token, "\t%.*s refers to", LIT(ref->token.string));
					}
					error(e->token, "\t%.*s", LIT(e->token.string));
					type = t_invalid;
					break;
				}
			}
		}
#endif
	} break;

	case Entity_Procedure:
		o->mode = Addressing_Value;
		break;

	case Entity_Builtin:
		o->builtin_id = e->Builtin.id;
		o->mode = Addressing_Builtin;
		break;

	case Entity_ImportName:
		error(ast_node_token(n), "Use of import `%.*s` not in selector", LIT(e->ImportName.name));
		return;

	case Entity_Nil:
		o->mode = Addressing_Value;
		break;

	case Entity_ImplicitValue:
		o->mode = Addressing_Value;
		break;

	default:
		compiler_error("Compiler error: Unknown EntityKind");
		break;
	}

	o->type = type;
}

i64 check_array_count(Checker *c, AstNode *e) {
	PROF_PROC();

	if (e == NULL) {
		return 0;
	}
	Operand o = {};
	check_expr(c, &o, e);
	if (o.mode != Addressing_Constant) {
		if (o.mode != Addressing_Invalid) {
			error(ast_node_token(e), "Array count must be a constant");
		}
		return 0;
	}
	if (is_type_untyped(o.type) || is_type_integer(o.type)) {
		if (o.value.kind == ExactValue_Integer) {
			i64 count = o.value.value_integer;
			if (count >= 0) {
				return count;
			}
			error(ast_node_token(e), "Invalid array count");
			return 0;
		}
	}

	error(ast_node_token(e), "Array count must be an integer");
	return 0;
}

Type *check_type(Checker *c, AstNode *e, Type *named_type, CycleChecker *cycle_checker) {
	PROF_PROC();

	ExactValue null_value = {ExactValue_Invalid};
	Type *type = NULL;
	gbString err_str = NULL;
	defer (gb_string_free(err_str));

	switch (e->kind) {
	case_ast_node(i, Ident, e);
		Operand o = {};
		check_identifier(c, &o, e, named_type, cycle_checker);

		switch (o.mode) {
		case Addressing_Invalid:
			break;
		case Addressing_Type: {
			type = o.type;
			goto end;
		} break;
		case Addressing_NoValue:
			err_str = expr_to_string(e);
			error(ast_node_token(e), "`%s` used as a type", err_str);
			break;
		default:
			err_str = expr_to_string(e);
			error(ast_node_token(e), "`%s` used as a type when not a type", err_str);
			break;
		}
	case_end;

	case_ast_node(se, SelectorExpr, e);
		Operand o = {};
		check_selector(c, &o, e);

		switch (o.mode) {
		case Addressing_Invalid:
			break;
		case Addressing_Type:
			GB_ASSERT(o.type != NULL);
			type = o.type;
			goto end;
		case Addressing_NoValue:
			err_str = expr_to_string(e);
			error(ast_node_token(e), "`%s` used as a type", err_str);
			break;
		default:
			err_str = expr_to_string(e);
			error(ast_node_token(e), "`%s` is not a type", err_str);
			break;
		}
	case_end;

	case_ast_node(pe, ParenExpr, e);
		type = check_type(c, pe->expr, named_type, cycle_checker);
		goto end;
	case_end;

	case_ast_node(ue, UnaryExpr, e);
		if (ue->op.kind == Token_Pointer) {
			type = make_type_pointer(c->allocator, check_type(c, ue->expr));
			goto end;
		} else if (ue->op.kind == Token_Maybe) {
			type = make_type_maybe(c->allocator, check_type(c, ue->expr));
			goto end;
		}
	case_end;

	case_ast_node(pt, PointerType, e);
		Type *elem = check_type(c, pt->type);
		type = make_type_pointer(c->allocator, elem);
		goto end;
	case_end;

	case_ast_node(mt, MaybeType, e);
		Type *elem = check_type(c, mt->type);
		type = make_type_maybe(c->allocator, elem);
		goto end;
	case_end;

	case_ast_node(at, ArrayType, e);
		if (at->count != NULL) {
			Type *elem = check_type(c, at->elem, NULL, cycle_checker);
			type = make_type_array(c->allocator, elem, check_array_count(c, at->count));
		} else {
			Type *elem = check_type(c, at->elem);
			type = make_type_slice(c->allocator, elem);
		}
		goto end;
	case_end;


	case_ast_node(vt, VectorType, e);
		Type *elem = check_type(c, vt->elem);
		Type *be = base_type(elem);
		i64 count = check_array_count(c, vt->count);
		if (!is_type_boolean(be) && !is_type_numeric(be)) {
			err_str = type_to_string(elem);
			error(ast_node_token(vt->elem), "Vector element type must be numerical or a boolean. Got `%s`", err_str);
		}
		type = make_type_vector(c->allocator, elem, count);
		goto end;
	case_end;

	case_ast_node(st, StructType, e);
		type = make_type_struct(c->allocator);
		set_base_type(named_type, type);
		check_open_scope(c, e);
		check_struct_type(c, type, e, cycle_checker);
		check_close_scope(c);
		type->Record.node = e;
		goto end;
	case_end;

	case_ast_node(ut, UnionType, e);
		type = make_type_union(c->allocator);
		set_base_type(named_type, type);
		check_open_scope(c, e);
		check_union_type(c, type, e, cycle_checker);
		check_close_scope(c);
		type->Record.node = e;
		goto end;
	case_end;

	case_ast_node(rut, RawUnionType, e);
		type = make_type_raw_union(c->allocator);
		set_base_type(named_type, type);
		check_open_scope(c, e);
		check_raw_union_type(c, type, e, cycle_checker);
		check_close_scope(c);
		type->Record.node = e;
		goto end;
	case_end;

	case_ast_node(et, EnumType, e);
		type = make_type_enum(c->allocator);
		set_base_type(named_type, type);
		check_open_scope(c, e);
		check_enum_type(c, type, named_type, e);
		check_close_scope(c);
		type->Record.node = e;
		goto end;
	case_end;

	case_ast_node(pt, ProcType, e);
		type = alloc_type(c->allocator, Type_Proc);
		set_base_type(named_type, type);
		check_open_scope(c, e);
		check_procedure_type(c, type, e);
		check_close_scope(c);
		goto end;
	case_end;

	case_ast_node(ce, CallExpr, e);
		Operand o = {};
		check_expr_or_type(c, &o, e);
		if (o.mode == Addressing_Type) {
			type = o.type;
			goto end;
		}
	case_end;
	}
	err_str = expr_to_string(e);
	error(ast_node_token(e), "`%s` is not a type", err_str);

	type = t_invalid;
end:
	if (type == NULL) {
		type = t_invalid;
	}

	set_base_type(named_type, type);
	GB_ASSERT(is_type_typed(type));

	add_type_and_value(&c->info, e, Addressing_Type, type, null_value);
	return type;
}


b32 check_unary_op(Checker *c, Operand *o, Token op) {
	// TODO(bill): Handle errors correctly
	Type *type = base_type(base_vector_type(o->type));
	gbString str = NULL;
	defer (gb_string_free(str));
	switch (op.kind) {
	case Token_Add:
	case Token_Sub:
		if (!is_type_numeric(type)) {
			str = expr_to_string(o->expr);
			error(op, "Operator `%.*s` is not allowed with `%s`", LIT(op.string), str);
		}
		break;

	case Token_Xor:
		if (!is_type_integer(type)) {
			error(op, "Operator `%.*s` is only allowed with integers", LIT(op.string));
		}
		break;

	case Token_Not:
		if (!is_type_boolean(type)) {
			str = expr_to_string(o->expr);
			error(op, "Operator `%.*s` is only allowed on boolean expression", LIT(op.string));
		}
		break;

	default:
		error(op, "Unknown operator `%.*s`", LIT(op.string));
		return false;
	}

	return true;
}

b32 check_binary_op(Checker *c, Operand *o, Token op) {
	// TODO(bill): Handle errors correctly
	Type *type = base_type(base_vector_type(o->type));
	switch (op.kind) {
	case Token_Sub:
	case Token_SubEq:
		if (!is_type_numeric(type) && !is_type_pointer(type)) {
			error(op, "Operator `%.*s` is only allowed with numeric or pointer expressions", LIT(op.string));
			return false;
		}
		if (is_type_pointer(type)) {
			o->type = t_int;
		}
		if (base_type(type) == t_rawptr) {
			gbString str = type_to_string(type);
			defer (gb_string_free(str));
			error(ast_node_token(o->expr), "Invalid pointer type for pointer arithmetic: `%s`", str);
			return false;
		}
		break;

	case Token_Add:
	case Token_Mul:
	case Token_Quo:
	case Token_AddEq:
	case Token_MulEq:
	case Token_QuoEq:
		if (!is_type_numeric(type)) {
			error(op, "Operator `%.*s` is only allowed with numeric expressions", LIT(op.string));
			return false;
		}
		break;

	case Token_And:
	case Token_Or:
	case Token_AndEq:
	case Token_OrEq:
		if (!is_type_integer(type) && !is_type_boolean(type)) {
			error(op, "Operator `%.*s` is only allowed with integers or booleans", LIT(op.string));
			return false;
		}
		break;

	case Token_Mod:
	case Token_Xor:
	case Token_AndNot:
	case Token_ModEq:
	case Token_XorEq:
	case Token_AndNotEq:
		if (!is_type_integer(type)) {
			error(op, "Operator `%.*s` is only allowed with integers", LIT(op.string));
			return false;
		}
		break;

	case Token_CmpAnd:
	case Token_CmpOr:

	case Token_CmpAndEq:
	case Token_CmpOrEq:
		if (!is_type_boolean(type)) {
			error(op, "Operator `%.*s` is only allowed with boolean expressions", LIT(op.string));
			return false;
		}
		break;

	default:
		error(op, "Unknown operator `%.*s`", LIT(op.string));
		return false;
	}

	return true;

}
b32 check_value_is_expressible(Checker *c, ExactValue in_value, Type *type, ExactValue *out_value) {
	PROF_PROC();

	if (in_value.kind == ExactValue_Invalid) {
		// NOTE(bill): There's already been an error
		return true;
	}

	if (is_type_boolean(type)) {
		return in_value.kind == ExactValue_Bool;
	} else if (is_type_string(type)) {
		return in_value.kind == ExactValue_String;
	} else if (is_type_integer(type)) {
		ExactValue v = exact_value_to_integer(in_value);
		if (v.kind != ExactValue_Integer) {
			return false;
		}
		if (out_value) *out_value = v;
		i64 i = v.value_integer;
		u64 u = *cast(u64 *)&i;
		i64 s = 8*type_size_of(c->sizes, c->allocator, type);
		u64 umax = ~0ull;
		if (s < 64) {
			umax = (1ull << s) - 1ull;
		}
		i64 imax = (1ll << (s-1ll));


		switch (type->Basic.kind) {
		case Basic_i8:
		case Basic_i16:
		case Basic_i32:
		case Basic_i64:
		case Basic_int:
			return gb_is_between(i, -imax, imax-1);

		case Basic_u8:
		case Basic_u16:
		case Basic_u32:
		case Basic_u64:
		case Basic_uint:
			return !(u < 0 || u > umax);

		case Basic_UntypedInteger:
			return true;

		default: GB_PANIC("Compiler error: Unknown integer type!"); break;
		}
	} else if (is_type_float(type)) {
		ExactValue v = exact_value_to_float(in_value);
		if (v.kind != ExactValue_Float) {
			return false;
		}

		switch (type->Basic.kind) {
		case Basic_f32:
			if (out_value) *out_value = v;
			return true;

		case Basic_f64:
			if (out_value) *out_value = v;
			return true;

		case Basic_UntypedFloat:
			return true;
		}
	} else if (is_type_pointer(type)) {
		if (in_value.kind == ExactValue_Pointer) {
			return true;
		}
		if (in_value.kind == ExactValue_Integer) {
			return true;
		}
		if (out_value) *out_value = in_value;
	}


	return false;
}

void check_is_expressible(Checker *c, Operand *o, Type *type) {
	PROF_PROC();

	GB_ASSERT(type->kind == Type_Basic);
	GB_ASSERT(o->mode == Addressing_Constant);
	if (!check_value_is_expressible(c, o->value, type, &o->value)) {
		gbString a = expr_to_string(o->expr);
		gbString b = type_to_string(type);
		defer (gb_string_free(a));
		defer (gb_string_free(b));
		if (is_type_numeric(o->type) && is_type_numeric(type)) {
			if (!is_type_integer(o->type) && is_type_integer(type)) {
				error(ast_node_token(o->expr), "`%s` truncated to `%s`", a, b);
			} else {
				error(ast_node_token(o->expr), "`%s = %lld` overflows `%s`", a, o->value.value_integer, b);
			}
		} else {
			error(ast_node_token(o->expr), "Cannot convert `%s`  to `%s`", a, b);
		}

		o->mode = Addressing_Invalid;
	}
}

b32 check_is_expr_vector_index(Checker *c, AstNode *expr) {
	// HACK(bill): Handle this correctly. Maybe with a custom AddressingMode
	expr = unparen_expr(expr);
	if (expr->kind == AstNode_IndexExpr) {
		ast_node(ie, IndexExpr, expr);
		Type *t = type_of_expr(&c->info, ie->expr);
		if (t != NULL) {
			return is_type_vector(base_type(t));
		}
	}
	return false;
}

void check_unary_expr(Checker *c, Operand *o, Token op, AstNode *node) {
	PROF_PROC();

	switch (op.kind) {
	case Token_Pointer: { // Pointer address
		if (o->mode != Addressing_Variable ||
		    check_is_expr_vector_index(c, o->expr)) {
			ast_node(ue, UnaryExpr, node);
			gbString str = expr_to_string(ue->expr);
			defer (gb_string_free(str));
			error(op, "Cannot take the pointer address of `%s`", str);
			o->mode = Addressing_Invalid;
			return;
		}
		o->mode = Addressing_Value;
		o->type = make_type_pointer(c->allocator, o->type);
		return;
	}

	case Token_Maybe: { // Make maybe
		Type *t = default_type(o->type);
		b32 is_value =
			o->mode == Addressing_Variable ||
			o->mode == Addressing_Value ||
			o->mode == Addressing_Constant;

		if (!is_value || is_type_untyped(t)) {
			ast_node(ue, UnaryExpr, node);
			gbString str = expr_to_string(ue->expr);
			defer (gb_string_free(str));
			error(op, "Cannot convert `%s` to a maybe", str);
			o->mode = Addressing_Invalid;
			return;
		}
		o->mode = Addressing_Value;
		o->type = make_type_maybe(c->allocator, t);
		return;
	}
	}

	if (!check_unary_op(c, o, op)) {
		o->mode = Addressing_Invalid;
		return;
	}

	if (o->mode == Addressing_Constant) {
		Type *type = base_type(o->type);
		if (type->kind != Type_Basic) {
			gbString xt = type_to_string(o->type);
			gbString err_str = expr_to_string(node);
			defer (gb_string_free(xt));
			defer (gb_string_free(err_str));
			error(op, "Invalid type, `%s`, for constant unary expression `%s`", xt, err_str);
			o->mode = Addressing_Invalid;
			return;
		}


		i32 precision = 0;
		if (is_type_unsigned(type)) {
			precision = cast(i32)(8 * type_size_of(c->sizes, c->allocator, type));
		}
		o->value = exact_unary_operator_value(op, o->value, precision);

		if (is_type_typed(type)) {
			if (node != NULL) {
				o->expr = node;
			}
			check_is_expressible(c, o, type);
		}
		return;
	}

	o->mode = Addressing_Value;
}

void check_comparison(Checker *c, Operand *x, Operand *y, Token op) {
	PROF_PROC();

	gbTempArenaMemory tmp = gb_temp_arena_memory_begin(&c->tmp_arena);
	defer (gb_temp_arena_memory_end(tmp));

	gbString err_str = NULL;
	defer (if (err_str != NULL) {
		gb_string_free(err_str);
	});

	if (check_is_assignable_to(c, x, y->type) ||
	    check_is_assignable_to(c, y, x->type)) {
		b32 defined = false;
		switch (op.kind) {
		case Token_CmpEq:
		case Token_NotEq:
			defined = is_type_comparable(get_enum_base_type(base_type(x->type)));
			break;
		case Token_Lt:
		case Token_Gt:
		case Token_LtEq:
		case Token_GtEq: {
			defined = is_type_ordered(get_enum_base_type(base_type(x->type)));
		} break;
		}

		if (!defined) {
			gbString type_string = type_to_string(x->type);
			defer (gb_string_free(type_string));
			err_str = gb_string_make(c->tmp_allocator,
			                         gb_bprintf("operator `%.*s` not defined for type `%s`", LIT(op.string), type_string));
		}
	} else {
		gbString xt = type_to_string(x->type);
		gbString yt = type_to_string(y->type);
		defer(gb_string_free(xt));
		defer(gb_string_free(yt));
		err_str = gb_string_make(c->tmp_allocator,
		                         gb_bprintf("mismatched types `%s` and `%s`", xt, yt));
	}

	if (err_str != NULL) {
		error(ast_node_token(x->expr), "Cannot compare expression, %s", err_str);
		x->type = t_untyped_bool;
		return;
	}

	if (x->mode == Addressing_Constant &&
	    y->mode == Addressing_Constant) {
		x->value = make_exact_value_bool(compare_exact_values(op, x->value, y->value));
	} else {
		x->mode = Addressing_Value;

		update_expr_type(c, x->expr, default_type(x->type), true);
		update_expr_type(c, y->expr, default_type(y->type), true);
	}

	if (is_type_vector(base_type(y->type))) {
		x->type = make_type_vector(c->allocator, t_bool, base_type(y->type)->Vector.count);
	} else {
		x->type = t_untyped_bool;
	}
}

void check_shift(Checker *c, Operand *x, Operand *y, AstNode *node) {
	PROF_PROC();

	GB_ASSERT(node->kind == AstNode_BinaryExpr);
	ast_node(be, BinaryExpr, node);

	ExactValue x_val = {};
	if (x->mode == Addressing_Constant) {
		x_val = exact_value_to_integer(x->value);
	}

	b32 x_is_untyped = is_type_untyped(x->type);
	if (!(is_type_integer(x->type) || (x_is_untyped && x_val.kind == ExactValue_Integer))) {
		gbString err_str = expr_to_string(x->expr);
		defer (gb_string_free(err_str));
		error(ast_node_token(node),
		      "Shifted operand `%s` must be an integer", err_str);
		x->mode = Addressing_Invalid;
		return;
	}

	if (is_type_unsigned(y->type)) {

	} else if (is_type_untyped(y->type)) {
		convert_to_typed(c, y, t_untyped_integer);
		if (y->mode == Addressing_Invalid) {
			x->mode = Addressing_Invalid;
			return;
		}
	} else {
		gbString err_str = expr_to_string(y->expr);
		defer (gb_string_free(err_str));
		error(ast_node_token(node),
		      "Shift amount `%s` must be an unsigned integer", err_str);
		x->mode = Addressing_Invalid;
		return;
	}


	if (x->mode == Addressing_Constant) {
		if (y->mode == Addressing_Constant) {
			ExactValue y_val = exact_value_to_integer(y->value);
			if (y_val.kind != ExactValue_Integer) {
				gbString err_str = expr_to_string(y->expr);
				defer (gb_string_free(err_str));
				error(ast_node_token(node),
				      "Shift amount `%s` must be an unsigned integer", err_str);
				x->mode = Addressing_Invalid;
				return;
			}

			u64 amount = cast(u64)y_val.value_integer;
			if (amount > 1074) {
				gbString err_str = expr_to_string(y->expr);
				defer (gb_string_free(err_str));
				error(ast_node_token(node),
				      "Shift amount too large: `%s`", err_str);
				x->mode = Addressing_Invalid;
				return;
			}

			if (!is_type_integer(x->type)) {
				// NOTE(bill): It could be an untyped float but still representable
				// as an integer
				x->type = t_untyped_integer;
			}

			x->value = exact_value_shift(be->op, x_val, make_exact_value_integer(amount));

			if (is_type_typed(x->type)) {
				check_is_expressible(c, x, base_type(x->type));
			}
			return;
		}

		if (x_is_untyped) {
			ExpressionInfo *info = map_get(&c->info.untyped, hash_pointer(x->expr));
			if (info != NULL) {
				info->is_lhs = true;
			}
			x->mode = Addressing_Value;
			return;
		}
	}

	if (y->mode == Addressing_Constant && y->value.value_integer < 0) {
		gbString err_str = expr_to_string(y->expr);
		defer (gb_string_free(err_str));
		error(ast_node_token(node),
		      "Shift amount cannot be negative: `%s`", err_str);
	}

	x->mode = Addressing_Value;
}

b32 check_is_castable_to(Checker *c, Operand *operand, Type *y) {
	PROF_PROC();

	if (check_is_assignable_to(c, operand, y)) {
		return true;
	}

	Type *x = operand->type;
	Type *xb = base_type(x);
	Type *yb = base_type(y);
	if (are_types_identical(xb, yb)) {
		return true;
	}
	xb = get_enum_base_type(x);
	yb = get_enum_base_type(y);


	// Cast between booleans and integers
	if (is_type_boolean(xb) || is_type_integer(xb)) {
		if (is_type_boolean(yb) || is_type_integer(yb)) {
			return true;
		}
	}

	// Cast between numbers
	if (is_type_integer(xb) || is_type_float(xb)) {
		if (is_type_integer(yb) || is_type_float(yb)) {
			return true;
		}
	}

	// Cast between pointers
	if (is_type_pointer(xb) && is_type_pointer(yb)) {
		return true;
	}

	// (u)int <-> pointer
	if (is_type_int_or_uint(xb) && is_type_rawptr(yb)) {
		return true;
	}
	if (is_type_rawptr(xb) && is_type_int_or_uint(yb)) {
		return true;
	}

	// []byte/[]u8 <-> string
	if (is_type_u8_slice(xb) && is_type_string(yb)) {
		return true;
	}
	if (is_type_string(xb) && is_type_u8_slice(yb)) {
		if (is_type_typed(xb)) {
			return true;
		}
	}

	// proc <-> proc
	if (is_type_proc(xb) && is_type_proc(yb)) {
		return true;
	}

	// proc -> rawptr
	if (is_type_proc(xb) && is_type_rawptr(yb)) {
		return true;
	}

	return false;
}

String check_down_cast_name(Type *dst_, Type *src_) {
	String result = {};
	Type *dst = type_deref(dst_);
	Type *src = type_deref(src_);
	Type *dst_s = base_type(dst);
	GB_ASSERT(is_type_struct(dst_s) || is_type_raw_union(dst_s));
	for (isize i = 0; i < dst_s->Record.field_count; i++) {
		Entity *f = dst_s->Record.fields[i];
		GB_ASSERT(f->kind == Entity_Variable && f->Variable.field);
		if (f->Variable.anonymous) {
			if (are_types_identical(f->type, src_)) {
				return f->token.string;
			}
			if (are_types_identical(type_deref(f->type), src_)) {
				return f->token.string;
			}

			if (!is_type_pointer(f->type)) {
				result = check_down_cast_name(f->type, src_);
				if (result.len > 0) {
					return result;
				}
			}
		}
	}

	return result;
}

Operand check_ptr_addition(Checker *c, TokenKind op, Operand *ptr, Operand *offset, AstNode *node) {
	GB_ASSERT(node->kind == AstNode_BinaryExpr);
	ast_node(be, BinaryExpr, node);
	GB_ASSERT(is_type_pointer(ptr->type));
	GB_ASSERT(is_type_integer(offset->type));
	GB_ASSERT(op == Token_Add || op == Token_Sub);

	Operand operand = {};
	operand.mode = Addressing_Value;
	operand.type = ptr->type;
	operand.expr = node;

	if (base_type(ptr->type) == t_rawptr) {
		gbString str = type_to_string(ptr->type);
		defer (gb_string_free(str));
		error(ast_node_token(node), "Invalid pointer type for pointer arithmetic: `%s`", str);
		operand.mode = Addressing_Invalid;
		return operand;
	}


	if (ptr->mode == Addressing_Constant && offset->mode == Addressing_Constant) {
		i64 elem_size = type_size_of(c->sizes, c->allocator, ptr->type);
		i64 ptr_val = ptr->value.value_pointer;
		i64 offset_val = exact_value_to_integer(offset->value).value_integer;
		i64 new_ptr_val = ptr_val;
		if (op == Token_Add) {
			new_ptr_val += elem_size*offset_val;
		} else {
			new_ptr_val -= elem_size*offset_val;
		}
		operand.mode = Addressing_Constant;
		operand.value = make_exact_value_pointer(new_ptr_val);
	}

	return operand;
}

void check_binary_expr(Checker *c, Operand *x, AstNode *node) {
	PROF_PROC();

	GB_ASSERT(node->kind == AstNode_BinaryExpr);
	Operand y_ = {}, *y = &y_;
	gbString err_str = NULL;
	defer (gb_string_free(err_str));


	ast_node(be, BinaryExpr, node);

	if (be->op.kind == Token_as) {
		check_expr(c, x, be->left);
		Type *type = check_type(c, be->right);
		if (x->mode == Addressing_Invalid) {
			return;
		}

		b32 is_const_expr = x->mode == Addressing_Constant;
		b32 can_convert = false;

		Type *bt = base_type(type);
		if (is_const_expr && is_type_constant_type(bt)) {
			if (bt->kind == Type_Basic) {
				if (check_value_is_expressible(c, x->value, bt, &x->value)) {
					can_convert = true;
				}
			}
		} else if (check_is_castable_to(c, x, type)) {
			if (x->mode != Addressing_Constant) {
				x->mode = Addressing_Value;
			}
			can_convert = true;
		}

		if (!can_convert) {
			gbString expr_str = expr_to_string(x->expr);
			gbString to_type  = type_to_string(type);
			gbString from_type = type_to_string(x->type);
			defer (gb_string_free(expr_str));
			defer (gb_string_free(to_type));
			defer (gb_string_free(from_type));
			error(ast_node_token(x->expr), "Cannot cast `%s` as `%s` from `%s`", expr_str, to_type, from_type);

			x->mode = Addressing_Invalid;
			return;
		}

		if (is_type_untyped(x->type)) {
			Type *final_type = type;
			if (is_const_expr && !is_type_constant_type(type)) {
				final_type = default_type(x->type);
			}
			update_expr_type(c, x->expr, final_type, true);
		}

		x->type = type;
		return;
	} else if (be->op.kind == Token_transmute) {
		check_expr(c, x, be->left);
		Type *type = check_type(c, be->right);
		if (x->mode == Addressing_Invalid) {
			return;
		}

		if (x->mode == Addressing_Constant) {
			gbString expr_str = expr_to_string(x->expr);
			defer (gb_string_free(expr_str));
			error(ast_node_token(x->expr), "Cannot transmute constant expression: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		if (is_type_untyped(x->type)) {
			gbString expr_str = expr_to_string(x->expr);
			defer (gb_string_free(expr_str));
			error(ast_node_token(x->expr), "Cannot transmute untyped expression: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		i64 srcz = type_size_of(c->sizes, c->allocator, x->type);
		i64 dstz = type_size_of(c->sizes, c->allocator, type);
		if (srcz != dstz) {
			gbString expr_str = expr_to_string(x->expr);
			gbString type_str = type_to_string(type);
			defer (gb_string_free(expr_str));
			defer (gb_string_free(type_str));
			error(ast_node_token(x->expr), "Cannot transmute `%s` to `%s`, %lld vs %lld bytes", expr_str, type_str, srcz, dstz);
			x->mode = Addressing_Invalid;
			return;
		}

		x->type = type;

		return;
	} else if (be->op.kind == Token_down_cast) {
		check_expr(c, x, be->left);
		Type *type = check_type(c, be->right);
		if (x->mode == Addressing_Invalid) {
			return;
		}

		if (x->mode == Addressing_Constant) {
			gbString expr_str = expr_to_string(node);
			defer (gb_string_free(expr_str));
			error(ast_node_token(node), "Cannot `down_cast` a constant expression: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		if (is_type_untyped(x->type)) {
			gbString expr_str = expr_to_string(node);
			defer (gb_string_free(expr_str));
			error(ast_node_token(node), "Cannot `down_cast` an untyped expression: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		if (!(is_type_pointer(x->type) && is_type_pointer(type))) {
			gbString expr_str = expr_to_string(node);
			defer (gb_string_free(expr_str));
			error(ast_node_token(node), "Can only `down_cast` pointers: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		Type *src = type_deref(x->type);
		Type *dst = type_deref(type);
		Type *bsrc = base_type(src);
		Type *bdst = base_type(dst);

		if (!(is_type_struct(bsrc) || is_type_raw_union(bsrc))) {
			gbString expr_str = expr_to_string(node);
			defer (gb_string_free(expr_str));
			error(ast_node_token(node), "Can only `down_cast` pointer from structs or unions: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		if (!(is_type_struct(bdst) || is_type_raw_union(bdst))) {
			gbString expr_str = expr_to_string(node);
			defer (gb_string_free(expr_str));
			error(ast_node_token(node), "Can only `down_cast` pointer to structs or unions: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		String param_name = check_down_cast_name(dst, src);
		if (param_name.len == 0) {
			gbString expr_str = expr_to_string(node);
			defer (gb_string_free(expr_str));
			error(ast_node_token(node), "Illegal `down_cast`: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		x->mode = Addressing_Value;
		x->type = type;
		return;
	} else if (be->op.kind == Token_union_cast) {
		check_expr(c, x, be->left);
		Type *type = check_type(c, be->right);
		if (x->mode == Addressing_Invalid) {
			return;
		}

		if (x->mode == Addressing_Constant) {
			gbString expr_str = expr_to_string(node);
			defer (gb_string_free(expr_str));
			error(ast_node_token(node), "Cannot `union_cast` a constant expression: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		if (is_type_untyped(x->type)) {
			gbString expr_str = expr_to_string(node);
			defer (gb_string_free(expr_str));
			error(ast_node_token(node), "Cannot `union_cast` an untyped expression: `%s`", expr_str);
			x->mode = Addressing_Invalid;
			return;
		}

		b32 src_is_ptr = is_type_pointer(x->type);
		b32 dst_is_ptr = is_type_pointer(type);
		Type *src = type_deref(x->type);
		Type *dst = type_deref(type);
		Type *bsrc = base_type(src);
		Type *bdst = base_type(dst);

		if (src_is_ptr != dst_is_ptr) {
			gbString src_type_str = type_to_string(x->type);
			gbString dst_type_str = type_to_string(type);
			defer (gb_string_free(src_type_str));
			defer (gb_string_free(dst_type_str));
			error(ast_node_token(node), "Invalid `union_cast` types: `%s` and `%s`", src_type_str, dst_type_str);
			x->mode = Addressing_Invalid;
			return;
		}

		if (!is_type_union(src)) {
			error(ast_node_token(node), "`union_cast` can only operate on unions");
			x->mode = Addressing_Invalid;
			return;
		}

		b32 ok = false;
		for (isize i = 1; i < bsrc->Record.field_count; i++) {
			Entity *f = bsrc->Record.fields[i];
			if (are_types_identical(f->type, dst)) {
				ok = true;
				break;
			}
		}

		if (!ok) {
			gbString expr_str = expr_to_string(node);
			gbString dst_type_str = type_to_string(type);
			defer (gb_string_free(expr_str));
			defer (gb_string_free(dst_type_str));
			error(ast_node_token(node), "Cannot `union_cast` `%s` to `%s`", expr_str, dst_type_str);
			x->mode = Addressing_Invalid;
			return;
		}

		Entity **variables = gb_alloc_array(c->allocator, Entity *, 2);
		Token tok = make_token_ident(make_string(""));
		variables[0] = make_entity_param(c->allocator, NULL, tok, type, false);
		variables[1] = make_entity_param(c->allocator, NULL, tok, t_bool, false);

		Type *tuple = make_type_tuple(c->allocator);
		tuple->Tuple.variables = variables;
		tuple->Tuple.variable_count = 2;

		x->type = tuple;
		x->mode = Addressing_Value;
		return;
	}

	check_expr(c, x, be->left);
	check_expr(c, y, be->right);
	if (x->mode == Addressing_Invalid) {
		return;
	}
	if (y->mode == Addressing_Invalid) {
		x->mode = Addressing_Invalid;
		x->expr = y->expr;
		return;
	}

	Token op = be->op;

	if (token_is_shift(op)) {
		check_shift(c, x, y, node);
		return;
	}

	if (op.kind == Token_Add   || op.kind == Token_Sub) {
		if (is_type_pointer(x->type) && is_type_integer(y->type)) {
			*x = check_ptr_addition(c, op.kind, x, y, node);
			return;
		} else if (is_type_integer(x->type) && is_type_pointer(y->type)) {
			if (op.kind == Token_Sub) {
				gbString lhs = expr_to_string(x->expr);
				gbString rhs = expr_to_string(y->expr);
				defer (gb_string_free(lhs));
				defer (gb_string_free(rhs));
				error(ast_node_token(node), "Invalid pointer arithmetic, did you mean `%s %.*s %s`?", rhs, LIT(op.string), lhs);
				x->mode = Addressing_Invalid;
				return;
			}
			*x = check_ptr_addition(c, op.kind, y, x, node);
			return;
		}
	}


	convert_to_typed(c, x, y->type);
	if (x->mode == Addressing_Invalid) {
		return;
	}
	convert_to_typed(c, y, x->type);
	if (y->mode == Addressing_Invalid) {
		x->mode = Addressing_Invalid;
		return;
	}

	if (token_is_comparison(op)) {
		check_comparison(c, x, y, op);
		return;
	}

	if (!are_types_identical(x->type, y->type)) {
		if (x->type != t_invalid &&
		    y->type != t_invalid) {
			gbString xt = type_to_string(x->type);
			gbString yt = type_to_string(y->type);
			defer (gb_string_free(xt));
			defer (gb_string_free(yt));
			err_str = expr_to_string(x->expr);
			error(op, "Mismatched types in binary expression `%s` : `%s` vs `%s`", err_str, xt, yt);
		}
		x->mode = Addressing_Invalid;
		return;
	}

	if (!check_binary_op(c, x, op)) {
		x->mode = Addressing_Invalid;
		return;
	}

	switch (op.kind) {
	case Token_Quo:
	case Token_Mod:
	case Token_QuoEq:
	case Token_ModEq:
		if ((x->mode == Addressing_Constant || is_type_integer(x->type)) &&
		    y->mode == Addressing_Constant) {
			b32 fail = false;
			switch (y->value.kind) {
			case ExactValue_Integer:
				if (y->value.value_integer == 0) {
					fail = true;
				}
				break;
			case ExactValue_Float:
				if (y->value.value_float == 0.0) {
					fail = true;
				}
				break;
			}

			if (fail) {
				error(ast_node_token(y->expr), "Division by zero not allowed");
				x->mode = Addressing_Invalid;
				return;
			}
		}
	}

	if (x->mode == Addressing_Constant &&
	    y->mode == Addressing_Constant) {
		ExactValue a = x->value;
		ExactValue b = y->value;

		Type *type = base_type(x->type);
		if (is_type_pointer(type)) {
			GB_ASSERT(op.kind == Token_Sub);
			i64 bytes = a.value_pointer - b.value_pointer;
			i64 diff = bytes/type_size_of(c->sizes, c->allocator, type);
			x->value = make_exact_value_pointer(diff);
			return;
		}

		if (type->kind != Type_Basic) {
			gbString xt = type_to_string(x->type);
			defer (gb_string_free(xt));
			err_str = expr_to_string(node);
			error(op, "Invalid type, `%s`, for constant binary expression `%s`", xt, err_str);
			x->mode = Addressing_Invalid;
			return;
		}

		if (op.kind == Token_Quo && is_type_integer(type)) {
			op.kind = Token_QuoEq; // NOTE(bill): Hack to get division of integers
		}
		x->value = exact_binary_operator_value(op, a, b);
		if (is_type_typed(type)) {
			if (node != NULL) {
				x->expr = node;
			}
			check_is_expressible(c, x, type);
		}
		return;
	}

	x->mode = Addressing_Value;
}


void update_expr_type(Checker *c, AstNode *e, Type *type, b32 final) {
	PROF_PROC();

	HashKey key = hash_pointer(e);
	ExpressionInfo *found = map_get(&c->info.untyped, key);
	if (found == NULL) {
		return;
	}

	switch (e->kind) {
	case_ast_node(ue, UnaryExpr, e);
		if (found->value.kind != ExactValue_Invalid) {
			break;
		}
		update_expr_type(c, ue->expr, type, final);
	case_end;

	case_ast_node(be, BinaryExpr, e);
		if (found->value.kind != ExactValue_Invalid) {
			break;
		}
		if (!token_is_comparison(be->op)) {
			if (token_is_shift(be->op)) {
				update_expr_type(c, be->left,  type, final);
			} else {
				update_expr_type(c, be->left,  type, final);
				update_expr_type(c, be->right, type, final);
			}
		}
	case_end;
	}

	if (!final && is_type_untyped(type)) {
		found->type = base_type(type);
		map_set(&c->info.untyped, key, *found);
	} else {
		ExpressionInfo old = *found;
		map_remove(&c->info.untyped, key);

		if (old.is_lhs && !is_type_integer(type)) {
			gbString expr_str = expr_to_string(e);
			gbString type_str = type_to_string(type);
			defer (gb_string_free(expr_str));
			defer (gb_string_free(type_str));
			error(ast_node_token(e), "Shifted operand %s must be an integer, got %s", expr_str, type_str);
			return;
		}

		add_type_and_value(&c->info, e, found->mode, type, found->value);
	}
}

void update_expr_value(Checker *c, AstNode *e, ExactValue value) {
	ExpressionInfo *found = map_get(&c->info.untyped, hash_pointer(e));
	if (found) {
		found->value = value;
	}
}

void convert_untyped_error(Checker *c, Operand *operand, Type *target_type) {
	gbString expr_str = expr_to_string(operand->expr);
	gbString type_str = type_to_string(target_type);
	char *extra_text = "";
	defer (gb_string_free(expr_str));
	defer (gb_string_free(type_str));

	if (operand->mode == Addressing_Constant) {
		if (operand->value.value_integer == 0) {
			if (make_string(expr_str) != "nil") { // HACK NOTE(bill): Just in case
				// NOTE(bill): Doesn't matter what the type is as it's still zero in the union
				extra_text = " - Did you want `nil`?";
			}
		}
	}
	error(ast_node_token(operand->expr), "Cannot convert `%s` to `%s`%s", expr_str, type_str, extra_text);

	operand->mode = Addressing_Invalid;
}

void convert_to_typed(Checker *c, Operand *operand, Type *target_type, i32 level) {
	PROF_PROC();

	GB_ASSERT_NOT_NULL(target_type);
	if (operand->mode == Addressing_Invalid ||
	    is_type_typed(operand->type) ||
	    target_type == t_invalid) {
		return;
	}

	if (is_type_untyped(target_type)) {
		Type *x = operand->type;
		Type *y = target_type;
		if (is_type_numeric(x) && is_type_numeric(y)) {
			if (x < y) {
				operand->type = target_type;
				update_expr_type(c, operand->expr, target_type, false);
			}
		} else if (x != y) {
			convert_untyped_error(c, operand, target_type);
		}
		return;
	}

	Type *t = get_enum_base_type(base_type(target_type));
	switch (t->kind) {
	case Type_Basic:
		if (operand->mode == Addressing_Constant) {
			check_is_expressible(c, operand, t);
			if (operand->mode == Addressing_Invalid) {
				return;
			}
			update_expr_value(c, operand->expr, operand->value);
		} else {
			switch (operand->type->Basic.kind) {
			case Basic_UntypedBool:
				if (!is_type_boolean(target_type)) {
					convert_untyped_error(c, operand, target_type);
					return;
				}
				break;
			case Basic_UntypedInteger:
			case Basic_UntypedFloat:
			case Basic_UntypedRune:
				if (!is_type_numeric(target_type)) {
					convert_untyped_error(c, operand, target_type);
					return;
				}
				break;

			case Basic_UntypedNil:
				if (!type_has_nil(target_type)) {
					convert_untyped_error(c, operand, target_type);
					return;
				}
				break;
			}
		}
		break;

	case Type_Maybe:
		if (is_type_untyped_nil(operand->type)) {
			// Okay
		} else if (level == 0) {
			convert_to_typed(c, operand, t->Maybe.elem, level+1);
			return;
		}

	default:
		if (!is_type_untyped_nil(operand->type) || !type_has_nil(target_type)) {
			convert_untyped_error(c, operand, target_type);
			return;
		}
		break;
	}



	operand->type = target_type;
}

b32 check_index_value(Checker *c, AstNode *index_value, i64 max_count, i64 *value) {
	PROF_PROC();

	Operand operand = {Addressing_Invalid};
	check_expr(c, &operand, index_value);
	if (operand.mode == Addressing_Invalid) {
		if (value) *value = 0;
		return false;
	}

	convert_to_typed(c, &operand, t_int);
	if (operand.mode == Addressing_Invalid) {
		if (value) *value = 0;
		return false;
	}

	if (!is_type_integer(get_enum_base_type(operand.type))) {
		gbString expr_str = expr_to_string(operand.expr);
		error(ast_node_token(operand.expr),
		            "Index `%s` must be an integer", expr_str);
		gb_string_free(expr_str);
		if (value) *value = 0;
		return false;
	}

	if (operand.mode == Addressing_Constant &&
	    (c->context.stmt_state_flags & StmtStateFlag_bounds_check) != 0) {
		i64 i = exact_value_to_integer(operand.value).value_integer;
		if (i < 0) {
			gbString expr_str = expr_to_string(operand.expr);
			error(ast_node_token(operand.expr),
			            "Index `%s` cannot be a negative value", expr_str);
			gb_string_free(expr_str);
			if (value) *value = 0;
			return false;
		}

		if (max_count >= 0) { // NOTE(bill): Do array bound checking
			if (value) *value = i;
			if (i >= max_count) {
				gbString expr_str = expr_to_string(operand.expr);
				error(ast_node_token(operand.expr),
				            "Index `%s` is out of bounds range 0..<%lld", expr_str, max_count);
				gb_string_free(expr_str);
				return false;
			}

			return true;
		}
	}

	// NOTE(bill): It's alright :D
	if (value) *value = -1;
	return true;
}

Entity *check_selector(Checker *c, Operand *operand, AstNode *node) {
	PROF_PROC();

	ast_node(se, SelectorExpr, node);

	b32 check_op_expr = true;
	Entity *expr_entity = NULL;
	Entity *entity = NULL;
	Selection sel = {}; // NOTE(bill): Not used if it's an import name

	AstNode *op_expr  = se->expr;
	AstNode *selector = unparen_expr(se->selector);
	if (selector == NULL) {
		goto error;
	}

	GB_ASSERT(selector->kind == AstNode_Ident);


	if (op_expr->kind == AstNode_Ident) {
		String name = op_expr->Ident.string;
		Entity *e = scope_lookup_entity(c->context.scope, name);
		add_entity_use(c, op_expr, e);
		expr_entity = e;
		if (e != NULL && e->kind == Entity_ImportName) {
			String sel_name = selector->Ident.string;
			check_op_expr = false;
			entity = scope_lookup_entity(e->ImportName.scope, sel_name);
			if (entity == NULL) {
				error(ast_node_token(op_expr), "`%.*s` is not declared by `%.*s`", LIT(sel_name), LIT(name));
				goto error;
			}
			if (entity->type == NULL) { // Not setup yet
				check_entity_decl(c, entity, NULL, NULL);
			}
			GB_ASSERT(entity->type != NULL);
			b32 is_not_exported = !is_entity_exported(entity);

			if (is_not_exported) {
				auto found = map_get(&e->ImportName.scope->implicit, hash_string(sel_name));
				if (!found && e->ImportName.scope != entity->scope) {
					is_not_exported = false;
				}
			}

			if (is_not_exported) {
				gbString sel_str = expr_to_string(selector);
				defer (gb_string_free(sel_str));
				error(ast_node_token(op_expr), "`%s` is not exported by `%.*s`", sel_str, LIT(name));
				// NOTE(bill): Not really an error so don't goto error
			}

			add_entity_use(c, selector, entity);
		}
	}
	if (check_op_expr) {
		check_expr_base(c, operand, op_expr);
		if (operand->mode == Addressing_Invalid) {
			goto error;
		}
	}


	if (entity == NULL) {
		sel = lookup_field(c->allocator, operand->type, selector->Ident.string, operand->mode == Addressing_Type);
		entity = sel.entity;
	}
	if (entity == NULL) {
		gbString op_str   = expr_to_string(op_expr);
		gbString type_str = type_to_string(operand->type);
		gbString sel_str  = expr_to_string(selector);
		defer (gb_string_free(op_str));
		defer (gb_string_free(type_str));
		defer (gb_string_free(sel_str));
		error(ast_node_token(op_expr), "`%s` (`%s`) has no field `%s`", op_str, type_str, sel_str);
		goto error;
	}

	if (expr_entity != NULL && expr_entity->kind == Entity_Constant && entity->kind != Entity_Constant) {
		gbString op_str   = expr_to_string(op_expr);
		gbString type_str = type_to_string(operand->type);
		gbString sel_str  = expr_to_string(selector);
		defer (gb_string_free(op_str));
		defer (gb_string_free(type_str));
		defer (gb_string_free(sel_str));
		error(ast_node_token(op_expr), "Cannot access non-constant field `%s` from `%s`", sel_str, op_str);
		goto error;
	}


	add_entity_use(c, selector, entity);

	switch (entity->kind) {
	case Entity_Constant:
		operand->mode = Addressing_Constant;
		operand->value = entity->Constant.value;
		break;
	case Entity_Variable:
		// TODO(bill): This is the rule I need?
		if (sel.indirect || operand->mode != Addressing_Value) {
			operand->mode = Addressing_Variable;
		}
		break;
	case Entity_TypeName:
		operand->mode = Addressing_Type;
		break;
	case Entity_Procedure:
		operand->mode = Addressing_Value;
		break;
	case Entity_Builtin:
		operand->mode = Addressing_Builtin;
		operand->builtin_id = entity->Builtin.id;
		break;

	// NOTE(bill): These cases should never be hit but are here for sanity reasons
	case Entity_Nil:
		operand->mode = Addressing_Value;
		break;
	case Entity_ImplicitValue:
		operand->mode = Addressing_Value;
		break;
	}

	operand->type = entity->type;
	operand->expr = node;

	return entity;

error:
	operand->mode = Addressing_Invalid;
	operand->expr = node;
	return NULL;
}

b32 check_builtin_procedure(Checker *c, Operand *operand, AstNode *call, i32 id) {
	PROF_PROC();

	GB_ASSERT(call->kind == AstNode_CallExpr);
	ast_node(ce, CallExpr, call);
	BuiltinProc *bp = &builtin_procs[id];
	{
		char *err = NULL;
		if (ce->args.count < bp->arg_count) {
			err = "Too few";
		} else if (ce->args.count > bp->arg_count && !bp->variadic) {
			err = "Too many";
		}

		if (err) {
			ast_node(proc, Ident, ce->proc);
			error(ce->close, "`%s` arguments for `%.*s`, expected %td, got %td",
			      err, LIT(proc->string),
			      bp->arg_count, ce->args.count);
			return false;
		}
	}

	switch (id) {
	case BuiltinProc_new:
	case BuiltinProc_new_slice:
	case BuiltinProc_size_of:
	case BuiltinProc_align_of:
	case BuiltinProc_offset_of:
	case BuiltinProc_type_info:
		// NOTE(bill): The first arg may be a Type, this will be checked case by case
		break;
	default:
		check_multi_expr(c, operand, ce->args[0]);
	}

	switch (id) {
	case BuiltinProc_new: {
		// new :: proc(Type) -> ^Type
		Operand op = {};
		check_expr_or_type(c, &op, ce->args[0]);
		Type *type = op.type;
		if ((op.mode != Addressing_Type && type == NULL) || type == t_invalid) {
			error(ast_node_token(ce->args[0]), "Expected a type for `new`");
			return false;
		}
		operand->mode = Addressing_Value;
		operand->type = make_type_pointer(c->allocator, type);
	} break;
	case BuiltinProc_new_slice: {
		// new_slice :: proc(Type, len: int[, cap: int]) -> []Type
		Operand op = {};
		check_expr_or_type(c, &op, ce->args[0]);
		Type *type = op.type;
		if ((op.mode != Addressing_Type && type == NULL) || type == t_invalid) {
			error(ast_node_token(ce->args[0]), "Expected a type for `new_slice`");
			return false;
		}

		AstNode *len = ce->args[1];
		AstNode *cap = NULL;
		if (ce->args.count > 2) {
			cap = ce->args[2];
		}

		check_expr(c, &op, len);
		if (op.mode == Addressing_Invalid) {
			return false;
		}
		if (!is_type_integer(op.type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Length for `new_slice` must be an integer, got `%s`",
			      type_str);
			return false;
		}

		if (cap != NULL) {
			check_expr(c, &op, cap);
			if (op.mode == Addressing_Invalid) {
				return false;
			}
			if (!is_type_integer(op.type)) {
				gbString type_str = type_to_string(operand->type);
				defer (gb_string_free(type_str));
				error(ast_node_token(call),
				      "Capacity for `new_slice` must be an integer, got `%s`",
				      type_str);
				return false;
			}
			if (ce->args.count > 3) {
				error(ast_node_token(call),
				      "Too many arguments to `new_slice`, expected either 2 or 3");
				return false;
			}
		}

		operand->mode = Addressing_Value;
		operand->type = make_type_slice(c->allocator, type);
	} break;

	case BuiltinProc_size_of: {
		// size_of :: proc(Type) -> int
		Type *type = check_type(c, ce->args[0]);
		if (type == NULL || type == t_invalid) {
			error(ast_node_token(ce->args[0]), "Expected a type for `size_of`");
			return false;
		}

		operand->mode = Addressing_Constant;
		operand->value = make_exact_value_integer(type_size_of(c->sizes, c->allocator, type));
		operand->type = t_int;

	} break;

	case BuiltinProc_size_of_val:
		// size_of_val :: proc(val: Type) -> int
		check_assignment(c, operand, NULL, make_string("argument of `size_of_val`"));
		if (operand->mode == Addressing_Invalid) {
			return false;
		}

		operand->mode = Addressing_Constant;
		operand->value = make_exact_value_integer(type_size_of(c->sizes, c->allocator, operand->type));
		operand->type = t_int;
		break;

	case BuiltinProc_align_of: {
		// align_of :: proc(Type) -> int
		Type *type = check_type(c, ce->args[0]);
		if (type == NULL || type == t_invalid) {
			error(ast_node_token(ce->args[0]), "Expected a type for `align_of`");
			return false;
		}
		operand->mode = Addressing_Constant;
		operand->value = make_exact_value_integer(type_align_of(c->sizes, c->allocator, type));
		operand->type = t_int;
	} break;

	case BuiltinProc_align_of_val:
		// align_of_val :: proc(val: Type) -> int
		check_assignment(c, operand, NULL, make_string("argument of `align_of_val`"));
		if (operand->mode == Addressing_Invalid) {
			return false;
		}

		operand->mode = Addressing_Constant;
		operand->value = make_exact_value_integer(type_align_of(c->sizes, c->allocator, operand->type));
		operand->type = t_int;
		break;

	case BuiltinProc_offset_of: {
		// offset_of :: proc(Type, field) -> int
		Operand op = {};
		Type *bt = check_type(c, ce->args[0]);
		Type *type = base_type(bt);
		if (type == NULL || type == t_invalid) {
			error(ast_node_token(ce->args[0]), "Expected a type for `offset_of`");
			return false;
		}

		AstNode *field_arg = unparen_expr(ce->args[1]);
		if (field_arg == NULL ||
		    field_arg->kind != AstNode_Ident) {
			error(ast_node_token(field_arg), "Expected an identifier for field argument");
			return false;
		}
		if (is_type_array(type) || is_type_vector(type)) {
			error(ast_node_token(field_arg), "Invalid type for `offset_of`");
			return false;
		}


		ast_node(arg, Ident, field_arg);
		Selection sel = lookup_field(c->allocator, type, arg->string, operand->mode == Addressing_Type);
		if (sel.entity == NULL) {
			gbString type_str = type_to_string(bt);
			defer (gb_string_free(type_str));
			error(ast_node_token(ce->args[0]),
			      "`%s` has no field named `%.*s`", type_str, LIT(arg->string));
			return false;
		}
		if (sel.indirect) {
			gbString type_str = type_to_string(bt);
			defer (gb_string_free(type_str));
			error(ast_node_token(ce->args[0]),
			      "Field `%.*s` is embedded via a pointer in `%s`", LIT(arg->string), type_str);
			return false;
		}

		operand->mode = Addressing_Constant;
		operand->value = make_exact_value_integer(type_offset_of_from_selection(c->sizes, c->allocator, type, sel));
		operand->type  = t_int;
	} break;

	case BuiltinProc_offset_of_val: {
		// offset_of_val :: proc(val: expression) -> int
		AstNode *arg = unparen_expr(ce->args[0]);
		if (arg->kind != AstNode_SelectorExpr) {
			gbString str = expr_to_string(arg);
			error(ast_node_token(arg), "`%s` is not a selector expression", str);
			return false;
		}
		ast_node(s, SelectorExpr, arg);

		check_expr(c, operand, s->expr);
		if (operand->mode == Addressing_Invalid) {
			return false;
		}

		Type *type = operand->type;
		if (base_type(type)->kind == Type_Pointer) {
			Type *p = base_type(type);
			if (is_type_struct(p)) {
				type = p->Pointer.elem;
			}
		}
		if (is_type_array(type) || is_type_vector(type)) {
			error(ast_node_token(arg), "Invalid type for `offset_of_val`");
			return false;
		}

		ast_node(i, Ident, s->selector);
		Selection sel = lookup_field(c->allocator, type, i->string, operand->mode == Addressing_Type);
		if (sel.entity == NULL) {
			gbString type_str = type_to_string(type);
			error(ast_node_token(arg),
			      "`%s` has no field named `%.*s`", type_str, LIT(i->string));
			return false;
		}
		if (sel.indirect) {
			gbString type_str = type_to_string(type);
			defer (gb_string_free(type_str));
			error(ast_node_token(ce->args[0]),
			      "Field `%.*s` is embedded via a pointer in `%s`", LIT(i->string), type_str);
			return false;
		}


		operand->mode = Addressing_Constant;
		// IMPORTANT TODO(bill): Fix for anonymous fields
		operand->value = make_exact_value_integer(type_offset_of_from_selection(c->sizes, c->allocator, type, sel));
		operand->type  = t_int;
	} break;

	case BuiltinProc_type_of_val:
		// type_of_val :: proc(val: Type) -> type(Type)
		check_assignment(c, operand, NULL, make_string("argument of `type_of_val`"));
		if (operand->mode == Addressing_Invalid || operand->mode == Addressing_Builtin) {
			return false;
		}
		operand->mode = Addressing_Type;
		break;


	case BuiltinProc_type_info: {
		// type_info :: proc(Type) -> ^Type_Info
		AstNode *expr = ce->args[0];
		Type *type = check_type(c, expr);
		if (type == NULL || type == t_invalid) {
			error(ast_node_token(expr), "Invalid argument to `type_info`");
			return false;
		}

		add_type_info_type(c, type);

		operand->mode = Addressing_Value;
		operand->type = t_type_info_ptr;
	} break;

	case BuiltinProc_type_info_of_val: {
		// type_info_of_val :: proc(val: Type) -> ^Type_Info
		AstNode *expr = ce->args[0];

		check_assignment(c, operand, NULL, make_string("argument of `type_info_of_val`"));
		if (operand->mode == Addressing_Invalid || operand->mode == Addressing_Builtin)
			return false;
		add_type_info_type(c, operand->type);

		operand->mode = Addressing_Value;
		operand->type = t_type_info_ptr;
	} break;



	case BuiltinProc_compile_assert:
		// compile_assert :: proc(cond: bool)

		if (!is_type_boolean(operand->type) && operand->mode != Addressing_Constant) {
			gbString str = expr_to_string(ce->args[0]);
			defer (gb_string_free(str));
			error(ast_node_token(call),
			      "`%s` is not a constant boolean", str);
			return false;
		}
		if (!operand->value.value_bool) {
			gbString str = expr_to_string(ce->args[0]);
			defer (gb_string_free(str));
			error(ast_node_token(call),
			      "Compile time assertion: `%s`", str);
		}
		break;

	case BuiltinProc_assert:
		// assert :: proc(cond: bool)

		if (!is_type_boolean(operand->type)) {
			gbString str = expr_to_string(ce->args[0]);
			defer (gb_string_free(str));
			error(ast_node_token(call),
			      "`%s` is not a boolean", str);
			return false;
		}

		operand->mode = Addressing_NoValue;
		break;

	case BuiltinProc_panic:
		// panic :: proc(msg: string)

		if (!is_type_string(operand->type)) {
			gbString str = expr_to_string(ce->args[0]);
			defer (gb_string_free(str));
			error(ast_node_token(call),
			      "`%s` is not a string", str);
			return false;
		}

		operand->mode = Addressing_NoValue;
		break;

	case BuiltinProc_copy: {
		// copy :: proc(x, y: []Type) -> int
		Type *dest_type = NULL, *src_type = NULL;

		Type *d = base_type(operand->type);
		if (d->kind == Type_Slice)
			dest_type = d->Slice.elem;

		Operand op = {};
		check_expr(c, &op, ce->args[1]);
		if (op.mode == Addressing_Invalid)
			return false;
		Type *s = base_type(op.type);
		if (s->kind == Type_Slice)
			src_type = s->Slice.elem;

		if (dest_type == NULL || src_type == NULL) {
			error(ast_node_token(call), "`copy` only expects slices as arguments");
			return false;
		}

		if (!are_types_identical(dest_type, src_type)) {
			gbString d_arg = expr_to_string(ce->args[0]);
			gbString s_arg = expr_to_string(ce->args[1]);
			gbString d_str = type_to_string(dest_type);
			gbString s_str = type_to_string(src_type);
			defer (gb_string_free(d_arg));
			defer (gb_string_free(s_arg));
			defer (gb_string_free(d_str));
			defer (gb_string_free(s_str));
			error(ast_node_token(call),
			      "Arguments to `copy`, %s, %s, have different elem types: %s vs %s",
			      d_arg, s_arg, d_str, s_str);
			return false;
		}

		operand->type = t_int; // Returns number of elems copied
		operand->mode = Addressing_Value;
	} break;

	case BuiltinProc_append: {
		// append :: proc(x : ^[]Type, y : Type) -> bool
		Type *x_type = NULL, *y_type = NULL;
		x_type = base_type(operand->type);

		Operand op = {};
		check_expr(c, &op, ce->args[1]);
		if (op.mode == Addressing_Invalid)
			return false;
		y_type = base_type(op.type);

		if (!(is_type_pointer(x_type) && is_type_slice(x_type->Pointer.elem))) {
			error(ast_node_token(call), "First argument to `append` must be a pointer to a slice");
			return false;
		}

		Type *elem_type = x_type->Pointer.elem->Slice.elem;
		if (!check_is_assignable_to(c, &op, elem_type)) {
			gbString d_arg = expr_to_string(ce->args[0]);
			gbString s_arg = expr_to_string(ce->args[1]);
			gbString d_str = type_to_string(elem_type);
			gbString s_str = type_to_string(y_type);
			defer (gb_string_free(d_arg));
			defer (gb_string_free(s_arg));
			defer (gb_string_free(d_str));
			defer (gb_string_free(s_str));
			error(ast_node_token(call),
			      "Arguments to `append`, %s, %s, have different element types: %s vs %s",
			      d_arg, s_arg, d_str, s_str);
			return false;
		}

		operand->type = t_bool; // Returns if it was successful
		operand->mode = Addressing_Value;
	} break;

	case BuiltinProc_swizzle: {
		// swizzle :: proc(v: {N}T, T...) -> {M}T
		Type *vector_type = base_type(operand->type);
		if (!is_type_vector(vector_type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "You can only `swizzle` a vector, got `%s`",
			      type_str);
			return false;
		}

		isize max_count = vector_type->Vector.count;
		isize arg_count = 0;
		for_array(i, ce->args) {
			if (i == 0) continue;
			AstNode *arg = ce->args[i];
			Operand op = {};
			check_expr(c, &op, arg);
			if (op.mode == Addressing_Invalid)
				return false;
			Type *arg_type = base_type(op.type);
			if (!is_type_integer(arg_type) || op.mode != Addressing_Constant) {
				error(ast_node_token(op.expr), "Indices to `swizzle` must be constant integers");
				return false;
			}

			if (op.value.value_integer < 0) {
				error(ast_node_token(op.expr), "Negative `swizzle` index");
				return false;
			}

			if (max_count <= op.value.value_integer) {
				error(ast_node_token(op.expr), "`swizzle` index exceeds vector length");
				return false;
			}

			arg_count++;
		}

		if (arg_count > max_count) {
			error(ast_node_token(call), "Too many `swizzle` indices, %td > %td", arg_count, max_count);
			return false;
		}

		Type *elem_type = vector_type->Vector.elem;
		operand->type = make_type_vector(c->allocator, elem_type, arg_count);
		operand->mode = Addressing_Value;
	} break;

#if 0
	case BuiltinProc_ptr_offset: {
		// ptr_offset :: proc(ptr: ^T, offset: int) -> ^T
		// ^T cannot be rawptr
		Type *ptr_type = base_type(operand->type);
		if (!is_type_pointer(ptr_type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a pointer to `ptr_offset`, got `%s`",
			      type_str);
			return false;
		}

		if (ptr_type == t_rawptr) {
			error(ast_node_token(call),
			      "`rawptr` cannot have pointer arithmetic");
			return false;
		}

		AstNode *offset = ce->args[1];
		Operand op = {};
		check_expr(c, &op, offset);
		if (op.mode == Addressing_Invalid)
			return false;
		Type *offset_type = base_type(op.type);
		if (!is_type_integer(offset_type)) {
			error(ast_node_token(op.expr), "Pointer offsets for `ptr_offset` must be an integer");
			return false;
		}

		if (operand->mode == Addressing_Constant &&
		    op.mode == Addressing_Constant) {
			i64 ptr = operand->value.value_pointer;
			i64 elem_size = type_size_of(c->sizes, c->allocator, ptr_type->Pointer.elem);
			ptr += elem_size * op.value.value_integer;
			operand->value.value_pointer = ptr;
		} else {
			operand->mode = Addressing_Value;
		}

	} break;

	case BuiltinProc_ptr_sub: {
		// ptr_sub :: proc(a, b: ^T) -> int
		// ^T cannot be rawptr
		Type *ptr_type = base_type(operand->type);
		if (!is_type_pointer(ptr_type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a pointer to `ptr_add`, got `%s`",
			      type_str);
			return false;
		}

		if (ptr_type == t_rawptr) {
			error(ast_node_token(call),
			      "`rawptr` cannot have pointer arithmetic");
			return false;
		}
		AstNode *offset = ce->args[1];
		Operand op = {};
		check_expr(c, &op, offset);
		if (op.mode == Addressing_Invalid)
			return false;
		if (!is_type_pointer(op.type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a pointer to `ptr_add`, got `%s`",
			      type_str);
			return false;
		}

		if (base_type(op.type) == t_rawptr) {
			error(ast_node_token(call),
			      "`rawptr` cannot have pointer arithmetic");
			return false;
		}

		if (!are_types_identical(operand->type, op.type)) {
			gbString a = type_to_string(operand->type);
			gbString b = type_to_string(op.type);
			defer (gb_string_free(a));
			defer (gb_string_free(b));
			error(ast_node_token(op.expr),
			      "`ptr_sub` requires to pointer of the same type. Got `%s` and `%s`.", a, b);
			return false;
		}

		operand->type = t_int;

		if (operand->mode == Addressing_Constant &&
		    op.mode == Addressing_Constant) {
			u8 *ptr_a = cast(u8 *)operand->value.value_pointer;
			u8 *ptr_b = cast(u8 *)op.value.value_pointer;
			isize elem_size = type_size_of(c->sizes, c->allocator, ptr_type->Pointer.elem);
			operand->value = make_exact_value_integer((ptr_a - ptr_b) / elem_size);
		} else {
			operand->mode = Addressing_Value;
		}
	} break;
#endif

	case BuiltinProc_slice_ptr: {
		// slice_ptr :: proc(a: ^T, len: int[, cap: int]) -> []T
		// ^T cannot be rawptr
		Type *ptr_type = base_type(operand->type);
		if (!is_type_pointer(ptr_type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a pointer to `slice_ptr`, got `%s`",
			      type_str);
			return false;
		}

		if (ptr_type == t_rawptr) {
			error(ast_node_token(call),
			      "`rawptr` cannot have pointer arithmetic");
			return false;
		}

		AstNode *len = ce->args[1];
		AstNode *cap = NULL;
		if (ce->args.count > 2) {
			cap = ce->args[2];
		}

		Operand op = {};
		check_expr(c, &op, len);
		if (op.mode == Addressing_Invalid)
			return false;
		if (!is_type_integer(op.type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Length for `slice_ptr` must be an integer, got `%s`",
			      type_str);
			return false;
		}

		if (cap != NULL) {
			check_expr(c, &op, cap);
			if (op.mode == Addressing_Invalid)
				return false;
			if (!is_type_integer(op.type)) {
				gbString type_str = type_to_string(operand->type);
				defer (gb_string_free(type_str));
				error(ast_node_token(call),
				      "Capacity for `slice_ptr` must be an integer, got `%s`",
				      type_str);
				return false;
			}
			if (ce->args.count > 3) {
				error(ast_node_token(call),
				      "Too many arguments to `slice_ptr`, expected either 2 or 3");
				return false;
			}
		}

		operand->type = make_type_slice(c->allocator, ptr_type->Pointer.elem);
		operand->mode = Addressing_Value;
	} break;

	case BuiltinProc_min: {
		// min :: proc(a, b: comparable) -> comparable
		Type *type = base_type(operand->type);
		if (!is_type_comparable(type) || !is_type_numeric(type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a comparable numeric type to `min`, got `%s`",
			      type_str);
			return false;
		}

		AstNode *other_arg = ce->args[1];
		Operand a = *operand;
		Operand b = {};
		check_expr(c, &b, other_arg);
		if (b.mode == Addressing_Invalid) {
			return false;
		}
		if (!is_type_comparable(b.type) || !is_type_numeric(type)) {
			gbString type_str = type_to_string(b.type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a comparable numeric type to `min`, got `%s`",
			      type_str);
			return false;
		}

		if (a.mode == Addressing_Constant &&
		    b.mode == Addressing_Constant) {
			ExactValue x = a.value;
			ExactValue y = b.value;
			Token lt = {Token_Lt};

			operand->mode = Addressing_Constant;
			if (compare_exact_values(lt, x, y)) {
				operand->value = x;
				operand->type = a.type;
			} else {
				operand->value = y;
				operand->type = b.type;
			}
		} else {
			operand->mode = Addressing_Value;
			operand->type = type;

			convert_to_typed(c, &a, b.type);
			if (a.mode == Addressing_Invalid) {
				return false;
			}
			convert_to_typed(c, &b, a.type);
			if (b.mode == Addressing_Invalid) {
				return false;
			}

			if (!are_types_identical(operand->type, b.type)) {
				gbString type_a = type_to_string(a.type);
				gbString type_b = type_to_string(b.type);
				defer (gb_string_free(type_a));
				defer (gb_string_free(type_b));
				error(ast_node_token(call),
				      "Mismatched types to `min`, `%s` vs `%s`",
				      type_a, type_b);
				return false;
			}
		}

	} break;

	case BuiltinProc_max: {
		// min :: proc(a, b: comparable) -> comparable
		Type *type = base_type(operand->type);
		if (!is_type_comparable(type) || !is_type_numeric(type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a comparable numeric type to `max`, got `%s`",
			      type_str);
			return false;
		}

		AstNode *other_arg = ce->args[1];
		Operand a = *operand;
		Operand b = {};
		check_expr(c, &b, other_arg);
		if (b.mode == Addressing_Invalid) {
			return false;
		}
		if (!is_type_comparable(b.type) || !is_type_numeric(type)) {
			gbString type_str = type_to_string(b.type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a comparable numeric type to `max`, got `%s`",
			      type_str);
			return false;
		}

		if (a.mode == Addressing_Constant &&
		    b.mode == Addressing_Constant) {
			ExactValue x = a.value;
			ExactValue y = b.value;
			Token gt = {Token_Gt};

			operand->mode = Addressing_Constant;
			if (compare_exact_values(gt, x, y)) {
				operand->value = x;
				operand->type = a.type;
			} else {
				operand->value = y;
				operand->type = b.type;
			}
		} else {
			operand->mode = Addressing_Value;
			operand->type = type;

			convert_to_typed(c, &a, b.type);
			if (a.mode == Addressing_Invalid) {
				return false;
			}
			convert_to_typed(c, &b, a.type);
			if (b.mode == Addressing_Invalid) {
				return false;
			}

			if (!are_types_identical(operand->type, b.type)) {
				gbString type_a = type_to_string(a.type);
				gbString type_b = type_to_string(b.type);
				defer (gb_string_free(type_a));
				defer (gb_string_free(type_b));
				error(ast_node_token(call),
				      "Mismatched types to `max`, `%s` vs `%s`",
				      type_a, type_b);
				return false;
			}
		}

	} break;

	case BuiltinProc_abs: {
		// abs :: proc(n: numeric) -> numeric
		Type *type = base_type(operand->type);
		if (!is_type_numeric(type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected a numeric type to `abs`, got `%s`",
			      type_str);
			return false;
		}

		if (operand->mode == Addressing_Constant) {
			switch (operand->value.kind) {
			case ExactValue_Integer:
				operand->value.value_integer = gb_abs(operand->value.value_integer);
				break;
			case ExactValue_Float:
				operand->value.value_float = gb_abs(operand->value.value_float);
				break;
			default:
				GB_PANIC("Invalid numeric constant");
				break;
			}
		} else {
			operand->mode = Addressing_Value;
		}

		operand->type = type;
	} break;

	case BuiltinProc_enum_to_string: {
		Type *type = base_type(operand->type);
		if (!is_type_enum(type)) {
			gbString type_str = type_to_string(operand->type);
			defer (gb_string_free(type_str));
			error(ast_node_token(call),
			      "Expected an enum to `enum_to_string`, got `%s`",
			      type_str);
			return false;
		}

		if (operand->mode == Addressing_Constant) {
			ExactValue value = make_exact_value_string(make_string(""));
			if (operand->value.kind == ExactValue_Integer) {
				i64 index = operand->value.value_integer;
				for (isize i = 0; i < type->Record.other_field_count; i++) {
					Entity *f = type->Record.other_fields[i];
					if (f->kind == Entity_Constant && f->Constant.value.kind == ExactValue_Integer) {
						i64 fv = f->Constant.value.value_integer;
						if (index == fv) {
							value = make_exact_value_string(f->token.string);
							break;
						}
					}
				}
			}

			operand->value = value;
			operand->type = t_string;
			return true;
		}

		add_type_info_type(c, operand->type);

		operand->mode = Addressing_Value;
		operand->type = t_string;
	} break;
	}

	return true;
}


void check_call_arguments(Checker *c, Operand *operand, Type *proc_type, AstNode *call) {
	PROF_PROC();

	GB_ASSERT(call->kind == AstNode_CallExpr);
	GB_ASSERT(proc_type->kind == Type_Proc);
	ast_node(ce, CallExpr, call);

	isize param_count = 0;
	b32 variadic = proc_type->Proc.variadic;
	b32 vari_expand = (ce->ellipsis.pos.line != 0);

	if (proc_type->Proc.params != NULL) {
		param_count = proc_type->Proc.params->Tuple.variable_count;
		if (variadic) {
			param_count--;
		}
	}

	if (vari_expand && !variadic) {
		error(ce->ellipsis,
		      "Cannot use `..` in call to a non-variadic procedure: `%.*s`",
		      LIT(ce->proc->Ident.string));
		return;
	}

	if (ce->args.count == 0 && param_count == 0) {
		return;
	}

	gbTempArenaMemory tmp = gb_temp_arena_memory_begin(&c->tmp_arena);
	defer (gb_temp_arena_memory_end(tmp));

	Array<Operand> operands;
	array_init(&operands, c->tmp_allocator, 2*param_count);

	for_array(i, ce->args) {
		Operand o = {};
		check_multi_expr(c, &o, ce->args[i]);
		if (o.type->kind != Type_Tuple) {
			array_add(&operands, o);
		} else {
			auto *tuple = &o.type->Tuple;
			if (variadic && i >= param_count) {
				error(ast_node_token(ce->args[i]),
				      "`..` in a variadic procedure cannot be applied to a %td-valued expression", tuple->variable_count);
				operand->mode = Addressing_Invalid;
				return;
			}
			for (isize j = 0; j < tuple->variable_count; j++) {
				o.type = tuple->variables[j]->type;
				array_add(&operands, o);
			}
		}
	}

	i32 error_code = 0;
	if (operands.count < param_count) {
		error_code = -1;
	} else if (!variadic && operands.count > param_count) {
		error_code = +1;
	}
	if (error_code != 0) {
		char *err_fmt = "Too many arguments for `%s`, expected %td arguments";
		if (error_code < 0) {
			err_fmt = "Too few arguments for `%s`, expected %td arguments";
		}

		gbString proc_str = expr_to_string(ce->proc);
		defer (gb_string_free(proc_str));
		error(ast_node_token(call), err_fmt, proc_str, param_count);
		operand->mode = Addressing_Invalid;
	}

	GB_ASSERT(proc_type->Proc.params != NULL);
	Entity **sig_params = proc_type->Proc.params->Tuple.variables;
	isize operand_index = 0;
	for (; operand_index < param_count; operand_index++) {
		Type *arg_type = sig_params[operand_index]->type;
		Operand o = operands[operand_index];
		if (variadic) {

			o = operands[operand_index];
		}
		check_assignment(c, &o, arg_type, make_string("argument"), true);
	}

	if (variadic) {
		b32 variadic_expand = false;
		Type *slice = sig_params[param_count]->type;
		Type *elem = base_type(slice)->Slice.elem;
		Type *t = elem;
		for (; operand_index < operands.count; operand_index++) {
			Operand o = operands[operand_index];
			if (vari_expand) {
				variadic_expand = true;
				t = slice;
				if (operand_index != param_count) {
					error(ast_node_token(o.expr),
					      "`..` in a variadic procedure can only have one variadic argument at the end");
					break;
				}
			}
			check_assignment(c, &o, t, make_string("argument"), true);
		}
	}
}


Entity *find_using_index_expr(Type *t) {
	t = base_type(t);
	if (t->kind != Type_Record) {
		return NULL;
	}

	for (isize i = 0; i < t->Record.field_count; i++) {
		Entity *f = t->Record.fields[i];
		if (f->kind == Entity_Variable &&
		    f->Variable.field && f->Variable.anonymous) {
			if (is_type_indexable(f->type)) {
				return f;
			}
			Entity *res = find_using_index_expr(f->type);
			if (res != NULL) {
				return res;
			}
		}
	}
	return NULL;
}

ExprKind check_call_expr(Checker *c, Operand *operand, AstNode *call) {
	PROF_PROC();

	GB_ASSERT(call->kind == AstNode_CallExpr);
	ast_node(ce, CallExpr, call);
	check_expr_or_type(c, operand, ce->proc);

	if (operand->mode == Addressing_Invalid) {
		for_array(i, ce->args) {
			check_expr_base(c, operand, ce->args[i]);
		}
		operand->mode = Addressing_Invalid;
		operand->expr = call;
		return Expr_Stmt;
	}


	if (operand->mode == Addressing_Builtin) {
		i32 id = operand->builtin_id;
		if (!check_builtin_procedure(c, operand, call, id)) {
			operand->mode = Addressing_Invalid;
		}
		operand->expr = call;
		return builtin_procs[id].kind;
	}

	Type *proc_type = base_type(operand->type);
	if (proc_type == NULL || proc_type->kind != Type_Proc) {
		AstNode *e = operand->expr;
		gbString str = expr_to_string(e);
		defer (gb_string_free(str));
		error(ast_node_token(e), "Cannot call a non-procedure: `%s`", str);

		operand->mode = Addressing_Invalid;
		operand->expr = call;

		return Expr_Stmt;
	}

	check_call_arguments(c, operand, proc_type, call);

	switch (proc_type->Proc.result_count) {
	case 0:
		operand->mode = Addressing_NoValue;
		break;
	case 1:
		operand->mode = Addressing_Value;
		operand->type = proc_type->Proc.results->Tuple.variables[0]->type;
		break;
	default:
		operand->mode = Addressing_Value;
		operand->type = proc_type->Proc.results;
		break;
	}

	operand->expr = call;
	return Expr_Stmt;
}

void check_expr_with_type_hint(Checker *c, Operand *o, AstNode *e, Type *t) {
	check_expr_base(c, o, e, t);
	check_not_tuple(c, o);
	char *err_str = NULL;
	switch (o->mode) {
	case Addressing_NoValue:
		err_str = "used as a value";
		break;
	case Addressing_Type:
		err_str = "is not an expression";
		break;
	case Addressing_Builtin:
		err_str = "must be called";
		break;
	}
	if (err_str != NULL) {
		gbString str = expr_to_string(e);
		defer (gb_string_free(str));
		error(ast_node_token(e), "`%s` %s", str, err_str);
		o->mode = Addressing_Invalid;
	}
}

ExprKind check__expr_base(Checker *c, Operand *o, AstNode *node, Type *type_hint) {
	ExprKind kind = Expr_Stmt;

	o->mode = Addressing_Invalid;
	o->type = t_invalid;

	switch (node->kind) {
	default:
		goto error;
		break;

	case_ast_node(be, BadExpr, node)
		goto error;
	case_end;

	case_ast_node(i, Ident, node);
		check_identifier(c, o, node, type_hint, NULL);
	case_end;

	case_ast_node(bl, BasicLit, node);
		Type *t = t_invalid;
		switch (bl->kind) {
		case Token_Integer: t = t_untyped_integer; break;
		case Token_Float:   t = t_untyped_float;   break;
		case Token_String:  t = t_untyped_string;  break;
		case Token_Rune:    t = t_untyped_rune;    break;
		default:            GB_PANIC("Unknown literal"); break;
		}
		o->mode  = Addressing_Constant;
		o->type  = t;
		o->value = make_exact_value_from_basic_literal(*bl);
	case_end;

	case_ast_node(pl, ProcLit, node);
		check_open_scope(c, pl->type);
		defer (check_close_scope(c));
		c->context.decl = make_declaration_info(c->allocator, c->context.scope);
		Type *proc_type = check_type(c, pl->type);
		if (proc_type != NULL) {
			check_proc_body(c, empty_token, c->context.decl, proc_type, pl->body);
			o->mode = Addressing_Value;
			o->type = proc_type;
		} else {
			gbString str = expr_to_string(node);
			error(ast_node_token(node), "Invalid procedure literal `%s`", str);
			gb_string_free(str);
			goto error;
		}
	case_end;

	case_ast_node(cl, CompoundLit, node);
		PROF_SCOPED("check__expr_base - CompoundLit");

		Type *type = type_hint;
		b32 ellipsis_array = false;
		b32 is_constant = true;
		if (cl->type != NULL) {
			type = NULL;

			// [..]Type
			if (cl->type->kind == AstNode_ArrayType && cl->type->ArrayType.count != NULL) {
				if (cl->type->ArrayType.count->kind == AstNode_Ellipsis) {
					type = make_type_array(c->allocator, check_type(c, cl->type->ArrayType.elem), -1);
					ellipsis_array = true;
				}
			}

			if (type == NULL) {
				type = check_type(c, cl->type);
			}
		}

		if (type == NULL) {
			error(ast_node_token(node), "Missing type in compound literal");
			goto error;
		}

		Type *t = base_type(type);
		switch (t->kind) {
		case Type_Record: {
			if (!is_type_struct(t)) {
				break;
			}
			if (cl->elems.count == 0) {
				break; // NOTE(bill): No need to init
			}
			{ // Checker values
				isize field_count = t->Record.field_count;
				if (cl->elems[0]->kind == AstNode_FieldValue) {
					b32 *fields_visited = gb_alloc_array(c->allocator, b32, field_count);

					for_array(i, cl->elems) {
						AstNode *elem = cl->elems[i];
						if (elem->kind != AstNode_FieldValue) {
							error(ast_node_token(elem),
							      "Mixture of `field = value` and value elements in a structure literal is not allowed");
							continue;
						}
						ast_node(fv, FieldValue, elem);
						if (fv->field->kind != AstNode_Ident) {
							gbString expr_str = expr_to_string(fv->field);
							defer (gb_string_free(expr_str));
							error(ast_node_token(elem),
							      "Invalid field name `%s` in structure literal", expr_str);
							continue;
						}
						String name = fv->field->Ident.string;

						Selection sel = lookup_field(c->allocator, type, name, o->mode == Addressing_Type);
						if (sel.entity == NULL) {
							error(ast_node_token(elem),
							      "Unknown field `%.*s` in structure literal", LIT(name));
							continue;
						}

						if (sel.index.count > 1) {
							error(ast_node_token(elem),
							      "Cannot assign to an anonymous field `%.*s` in a structure literal (at the moment)", LIT(name));
							continue;
						}

						Entity *field = t->Record.fields[sel.index[0]];
						add_entity_use(c, fv->field, field);

						if (fields_visited[sel.index[0]]) {
							error(ast_node_token(elem),
							      "Duplicate field `%.*s` in structure literal", LIT(name));
							continue;
						}

						fields_visited[sel.index[0]] = true;
						check_expr(c, o, fv->value);

						if (base_type(field->type) == t_any) {
							is_constant = false;
						}
						if (is_constant) {
							is_constant = o->mode == Addressing_Constant;
						}


						check_assignment(c, o, field->type, make_string("structure literal"));
					}
				} else {
					for_array(index, cl->elems) {
						AstNode *elem = cl->elems[index];
						if (elem->kind == AstNode_FieldValue) {
							error(ast_node_token(elem),
							      "Mixture of `field = value` and value elements in a structure literal is not allowed");
							continue;
						}
						Entity *field = t->Record.fields_in_src_order[index];

						check_expr(c, o, elem);
						if (index >= field_count) {
							error(ast_node_token(o->expr), "Too many values in structure literal, expected %td", field_count);
							break;
						}

						if (base_type(field->type) == t_any) {
							is_constant = false;
						}
						if (is_constant) {
							is_constant = o->mode == Addressing_Constant;
						}

						check_assignment(c, o, field->type, make_string("structure literal"));
					}
					if (cl->elems.count < field_count) {
						error(cl->close, "Too few values in structure literal, expected %td, got %td", field_count, cl->elems.count);
					}
				}
			}

		} break;

		case Type_Slice:
		case Type_Array:
		case Type_Vector:
		{
			Type *elem_type = NULL;
			String context_name = {};
			if (t->kind == Type_Slice) {
				elem_type = t->Slice.elem;
				context_name = make_string("slice literal");
			} else if (t->kind == Type_Vector) {
				elem_type = t->Vector.elem;
				context_name = make_string("vector literal");
			} else {
				elem_type = t->Array.elem;
				context_name = make_string("array literal");
			}


			i64 max = 0;
			isize index = 0;
			isize elem_count = cl->elems.count;

			if (base_type(elem_type) == t_any) {
				is_constant = false;
			}

			for (; index < elem_count; index++) {
				AstNode *e = cl->elems[index];
				if (e->kind == AstNode_FieldValue) {
					error(ast_node_token(e),
					      "`field = value` is only allowed in struct literals");
					continue;
				}

				if (t->kind == Type_Array &&
				    t->Array.count >= 0 &&
				    index >= t->Array.count) {
					error(ast_node_token(e), "Index %lld is out of bounds (>= %lld) for array literal", index, t->Array.count);
				}
				if (t->kind == Type_Vector &&
				    t->Vector.count >= 0 &&
				    index >= t->Vector.count) {
					error(ast_node_token(e), "Index %lld is out of bounds (>= %lld) for vector literal", index, t->Vector.count);
				}

				Operand operand = {};
				check_expr_with_type_hint(c, &operand, e, elem_type);
				check_assignment(c, &operand, elem_type, context_name);

				if (is_constant) {
					is_constant = operand.mode == Addressing_Constant;
				}
			}
			if (max < index) {
				max = index;
			}

			if (t->kind == Type_Vector) {
				if (t->Vector.count > 1 && gb_is_between(index, 2, t->Vector.count-1)) {
					error(ast_node_token(cl->elems[0]),
					      "Expected either 1 (broadcast) or %td elements in vector literal, got %td", t->Vector.count, index);
				}
			}

			if (t->kind == Type_Array && ellipsis_array) {
				t->Array.count = max;
			}
		} break;

		default: {
			gbString str = type_to_string(type);
			error(ast_node_token(node), "Invalid compound literal type `%s`", str);
			gb_string_free(str);
			goto error;
		} break;
		}

		if (is_constant) {
			o->mode = Addressing_Constant;
			o->value = make_exact_value_compound(node);
		} else {
			o->mode = Addressing_Value;
		}
		o->type = type;
	case_end;

	case_ast_node(pe, ParenExpr, node);
		kind = check_expr_base(c, o, pe->expr, type_hint);
		o->expr = node;
	case_end;


	case_ast_node(te, TagExpr, node);
		// TODO(bill): Tag expressions
		error(ast_node_token(node), "Tag expressions are not supported yet");
		kind = check_expr_base(c, o, te->expr, type_hint);
		o->expr = node;
	case_end;

	case_ast_node(re, RunExpr, node);
		// TODO(bill): Tag expressions
		kind = check_expr_base(c, o, re->expr, type_hint);
		o->expr = node;
	case_end;


	case_ast_node(ue, UnaryExpr, node);
		check_expr(c, o, ue->expr);
		if (o->mode == Addressing_Invalid) {
			goto error;
		}
		check_unary_expr(c, o, ue->op, node);
		if (o->mode == Addressing_Invalid) {
			goto error;
		}
	case_end;


	case_ast_node(be, BinaryExpr, node);
		check_binary_expr(c, o, node);
		if (o->mode == Addressing_Invalid) {
			goto error;
		}
	case_end;



	case_ast_node(se, SelectorExpr, node);
		check_selector(c, o, node);
	case_end;


	case_ast_node(ie, IndexExpr, node);
		PROF_SCOPED("check__expr_base - IndexExpr");

		check_expr(c, o, ie->expr);
		if (o->mode == Addressing_Invalid) {
			goto error;
		}

		Type *t = base_type(type_deref(o->type));
		b32 is_const = o->mode == Addressing_Constant;


		auto set_index_data = [](Operand *o, Type *t, i64 *max_count) -> b32 {
			t = base_type(type_deref(t));

			switch (t->kind) {
			case Type_Basic:
				if (is_type_string(t)) {
					if (o->mode == Addressing_Constant) {
						*max_count = o->value.value_string.len;
					}
					if (o->mode != Addressing_Variable) {
						o->mode = Addressing_Value;
					}
					o->type = t_u8;
					return true;
				}
				break;

			case Type_Array:
				*max_count = t->Array.count;
				if (o->mode != Addressing_Variable) {
					o->mode = Addressing_Value;
				}
				o->type = t->Array.elem;
				return true;

			case Type_Vector:
				*max_count = t->Vector.count;
				if (o->mode != Addressing_Variable) {
					o->mode = Addressing_Value;
				}
				o->type = t->Vector.elem;
				return true;


			case Type_Slice:
				o->type = t->Slice.elem;
				o->mode = Addressing_Variable;
				return true;
			}

			return false;
		};

		i64 max_count = -1;
		b32 valid = set_index_data(o, t, &max_count);

		if (is_const) {
			valid = false;
		}

		if (!valid && (is_type_struct(t) || is_type_raw_union(t))) {
			Entity *found = find_using_index_expr(t);
			if (found != NULL) {
				valid = set_index_data(o, found->type, &max_count);
			}
		}

		if (!valid) {
			gbString str = expr_to_string(o->expr);
			if (is_const) {
				error(ast_node_token(o->expr), "Cannot index a constant `%s`", str);
			} else {
				error(ast_node_token(o->expr), "Cannot index `%s`", str);
			}
			gb_string_free(str);
			goto error;
		}

		if (ie->index == NULL) {
			gbString str = expr_to_string(o->expr);
			error(ast_node_token(o->expr), "Missing index for `%s`", str);
			gb_string_free(str);
			goto error;
		}

		i64 index = 0;
		b32 ok = check_index_value(c, ie->index, max_count, &index);

	case_end;



	case_ast_node(se, SliceExpr, node);
		PROF_SCOPED("check__expr_base - SliceExpr");

		check_expr(c, o, se->expr);
		if (o->mode == Addressing_Invalid) {
			goto error;
		}

		b32 valid = false;
		i64 max_count = -1;
		Type *t = base_type(type_deref(o->type));
		switch (t->kind) {
		case Type_Basic:
			if (is_type_string(t)) {
				valid = true;
				if (o->mode == Addressing_Constant) {
					max_count = o->value.value_string.len;
				}
				if (se->max != NULL) {
					error(ast_node_token(se->max), "Max (3rd) index not needed in substring expression");
				}
				o->type = t_string;
			}
			break;

		case Type_Array:
			valid = true;
			max_count = t->Array.count;
			if (o->mode != Addressing_Variable) {
				gbString str = expr_to_string(node);
				error(ast_node_token(node), "Cannot slice array `%s`, value is not addressable", str);
				gb_string_free(str);
				goto error;
			}
			o->type = make_type_slice(c->allocator, t->Array.elem);
			break;

		case Type_Slice:
			valid = true;
			break;
		}

		if (!valid) {
			gbString str = expr_to_string(o->expr);
			error(ast_node_token(o->expr), "Cannot slice `%s`", str);
			gb_string_free(str);
			goto error;
		}

		o->mode = Addressing_Value;

		i64 indices[3] = {};
		AstNode *nodes[3] = {se->low, se->high, se->max};
		for (isize i = 0; i < gb_count_of(nodes); i++) {
			i64 index = max_count;
			if (nodes[i] != NULL) {
				i64 capacity = -1;
				if (max_count >= 0)
					capacity = max_count;
				i64 j = 0;
				if (check_index_value(c, nodes[i], capacity, &j)) {
					index = j;
				}
			} else if (i == 0) {
				index = 0;
			}
			indices[i] = index;
		}

		for (isize i = 0; i < gb_count_of(indices); i++) {
			i64 a = indices[i];
			for (isize j = i+1; j < gb_count_of(indices); j++) {
				i64 b = indices[j];
				if (a > b && b >= 0) {
					error(se->close, "Invalid slice indices: [%td > %td]", a, b);
				}
			}
		}

	case_end;


	case_ast_node(ce, CallExpr, node);
		return check_call_expr(c, o, node);
	case_end;

	case_ast_node(de, DerefExpr, node);
		check_expr_or_type(c, o, de->expr);
		if (o->mode == Addressing_Invalid) {
			goto error;
		} else {
			Type *t = base_type(o->type);
			if (t->kind == Type_Pointer) {
				o->mode = Addressing_Variable;
				o->type = t->Pointer.elem;
 			} else {
 				gbString str = expr_to_string(o->expr);
 				error(ast_node_token(o->expr), "Cannot dereference `%s`", str);
 				gb_string_free(str);
 				goto error;
 			}
		}
	case_end;

	case_ast_node(de, DemaybeExpr, node);
		check_expr_or_type(c, o, de->expr);
		if (o->mode == Addressing_Invalid) {
			goto error;
		} else {
			Type *t = base_type(o->type);
			if (t->kind == Type_Maybe) {
				Entity **variables = gb_alloc_array(c->allocator, Entity *, 2);
				Type *elem = t->Maybe.elem;
				Token tok = make_token_ident(make_string(""));
				variables[0] = make_entity_param(c->allocator, NULL, tok, elem, false);
				variables[1] = make_entity_param(c->allocator, NULL, tok, t_bool, false);

				Type *tuple = make_type_tuple(c->allocator);
				tuple->Tuple.variables = variables;
				tuple->Tuple.variable_count = 2;

				o->type = tuple;
				o->mode = Addressing_Variable;
 			} else {
 				gbString str = expr_to_string(o->expr);
 				error(ast_node_token(o->expr), "Cannot demaybe `%s`", str);
 				gb_string_free(str);
 				goto error;
 			}
		}
	case_end;

	case AstNode_ProcType:
	case AstNode_PointerType:
	case AstNode_MaybeType:
	case AstNode_ArrayType:
	case AstNode_VectorType:
	case AstNode_StructType:
	case AstNode_RawUnionType:
		o->mode = Addressing_Type;
		o->type = check_type(c, node);
		break;
	}

	kind = Expr_Expr;
	o->expr = node;
	return kind;

error:
	o->mode = Addressing_Invalid;
	o->expr = node;
	return kind;
}

ExprKind check_expr_base(Checker *c, Operand *o, AstNode *node, Type *type_hint) {
	ExprKind kind = check__expr_base(c, o, node, type_hint);
	Type *type = NULL;
	ExactValue value = {ExactValue_Invalid};
	switch (o->mode) {
	case Addressing_Invalid:
		type = t_invalid;
		break;
	case Addressing_NoValue:
		type = NULL;
		break;
	case Addressing_Constant:
		type = o->type;
		value = o->value;
		break;
	default:
		type = o->type;
		break;
	}

	if (type != NULL && is_type_untyped(type)) {
		add_untyped(&c->info, node, false, o->mode, type, value);
	} else {
		add_type_and_value(&c->info, node, o->mode, type, value);
	}
	return kind;
}


void check_multi_expr(Checker *c, Operand *o, AstNode *e) {
	gbString err_str = NULL;
	defer (gb_string_free(err_str));

	check_expr_base(c, o, e);
	switch (o->mode) {
	default:
		return; // NOTE(bill): Valid

	case Addressing_NoValue:
		err_str = expr_to_string(e);
		error(ast_node_token(e), "`%s` used as value", err_str);
		break;
	case Addressing_Type:
		err_str = expr_to_string(e);
		error(ast_node_token(e), "`%s` is not an expression", err_str);
		break;
	}
	o->mode = Addressing_Invalid;
}

void check_not_tuple(Checker *c, Operand *o) {
	if (o->mode == Addressing_Value) {
		// NOTE(bill): Tuples are not first class thus never named
		if (o->type->kind == Type_Tuple) {
			isize count = o->type->Tuple.variable_count;
			GB_ASSERT(count != 1);
			error(ast_node_token(o->expr),
			      "%td-valued tuple found where single value expected", count);
			o->mode = Addressing_Invalid;
		}
	}
}

void check_expr(Checker *c, Operand *o, AstNode *e) {
	check_multi_expr(c, o, e);
	check_not_tuple(c, o);
}


void check_expr_or_type(Checker *c, Operand *o, AstNode *e) {
	check_expr_base(c, o, e);
	check_not_tuple(c, o);
	if (o->mode == Addressing_NoValue) {
		gbString str = expr_to_string(o->expr);
		defer (gb_string_free(str));
		error(ast_node_token(o->expr),
		      "`%s` used as value or type", str);
		o->mode = Addressing_Invalid;
	}
}


gbString write_expr_to_string(gbString str, AstNode *node);

gbString write_params_to_string(gbString str, AstNodeArray params, char *sep) {
	for_array(i, params) {
		ast_node(p, Parameter, params[i]);
		if (i > 0) {
			str = gb_string_appendc(str, sep);
		}

		str = write_expr_to_string(str, params[i]);
	}
	return str;
}

gbString string_append_token(gbString str, Token token) {
	if (token.string.len > 0) {
		return gb_string_append_length(str, token.string.text, token.string.len);
	}
	return str;
}


gbString write_expr_to_string(gbString str, AstNode *node) {
	if (node == NULL)
		return str;

	if (is_ast_node_stmt(node)) {
		GB_ASSERT("stmt passed to write_expr_to_string");
	}

	switch (node->kind) {
	default:
		str = gb_string_appendc(str, "(BadExpr)");
		break;

	case_ast_node(i, Ident, node);
		str = string_append_token(str, *i);
	case_end;

	case_ast_node(bl, BasicLit, node);
		str = string_append_token(str, *bl);
	case_end;

	case_ast_node(pl, ProcLit, node);
		str = write_expr_to_string(str, pl->type);
	case_end;

	case_ast_node(cl, CompoundLit, node);
		str = write_expr_to_string(str, cl->type);
		str = gb_string_appendc(str, "{");
		for_array(i, cl->elems) {
			if (i > 0) {
				str = gb_string_appendc(str, ", ");
			}
			str = write_expr_to_string(str, cl->elems[i]);
		}
		str = gb_string_appendc(str, "}");
	case_end;

	case_ast_node(te, TagExpr, node);
		str = gb_string_appendc(str, "#");
		str = string_append_token(str, te->name);
		str = write_expr_to_string(str, te->expr);
	case_end;

	case_ast_node(ue, UnaryExpr, node);
		str = string_append_token(str, ue->op);
		str = write_expr_to_string(str, ue->expr);
	case_end;

	case_ast_node(de, DerefExpr, node);
		str = write_expr_to_string(str, de->expr);
		str = gb_string_appendc(str, "^");
	case_end;

	case_ast_node(de, DemaybeExpr, node);
		str = write_expr_to_string(str, de->expr);
		str = gb_string_appendc(str, "?");
	case_end;

	case_ast_node(be, BinaryExpr, node);
		str = write_expr_to_string(str, be->left);
		str = gb_string_appendc(str, " ");
		str = string_append_token(str, be->op);
		str = gb_string_appendc(str, " ");
		str = write_expr_to_string(str, be->right);
	case_end;

	case_ast_node(pe, ParenExpr, node);
		str = gb_string_appendc(str, "(");
		str = write_expr_to_string(str, pe->expr);
		str = gb_string_appendc(str, ")");
	case_end;

	case_ast_node(se, SelectorExpr, node);
		str = write_expr_to_string(str, se->expr);
		str = gb_string_appendc(str, ".");
		str = write_expr_to_string(str, se->selector);
	case_end;

	case_ast_node(ie, IndexExpr, node);
		str = write_expr_to_string(str, ie->expr);
		str = gb_string_appendc(str, "[");
		str = write_expr_to_string(str, ie->index);
		str = gb_string_appendc(str, "]");
	case_end;

	case_ast_node(se, SliceExpr, node);
		str = write_expr_to_string(str, se->expr);
		str = gb_string_appendc(str, "[");
		str = write_expr_to_string(str, se->low);
		str = gb_string_appendc(str, ":");
		str = write_expr_to_string(str, se->high);
		if (se->triple_indexed) {
			str = gb_string_appendc(str, ":");
			str = write_expr_to_string(str, se->max);
		}
		str = gb_string_appendc(str, "]");
	case_end;

	case_ast_node(e, Ellipsis, node);
		str = gb_string_appendc(str, "..");
	case_end;

	case_ast_node(fv, FieldValue, node);
		str = write_expr_to_string(str, fv->field);
		str = gb_string_appendc(str, " = ");
		str = write_expr_to_string(str, fv->value);
	case_end;

	case_ast_node(pt, PointerType, node);
		str = gb_string_appendc(str, "^");
		str = write_expr_to_string(str, pt->type);
	case_end;

	case_ast_node(mt, MaybeType, node);
		str = gb_string_appendc(str, "?");
		str = write_expr_to_string(str, mt->type);
	case_end;

	case_ast_node(at, ArrayType, node);
		str = gb_string_appendc(str, "[");
		str = write_expr_to_string(str, at->count);
		str = gb_string_appendc(str, "]");
		str = write_expr_to_string(str, at->elem);
	case_end;

	case_ast_node(vt, VectorType, node);
		str = gb_string_appendc(str, "{");
		str = write_expr_to_string(str, vt->count);
		str = gb_string_appendc(str, "}");
		str = write_expr_to_string(str, vt->elem);
	case_end;

	case_ast_node(p, Parameter, node);
		if (p->is_using) {
			str = gb_string_appendc(str, "using ");
		}
		for_array(i, p->names) {
			AstNode *name = p->names[i];
			if (i > 0)
				str = gb_string_appendc(str, ", ");
			str = write_expr_to_string(str, name);
		}

		str = gb_string_appendc(str, ": ");
		str = write_expr_to_string(str, p->type);
	case_end;

	case_ast_node(ce, CallExpr, node);
		str = write_expr_to_string(str, ce->proc);
		str = gb_string_appendc(str, "(");

		for_array(i, ce->args) {
			AstNode *arg = ce->args[i];
			if (i > 0) {
				str = gb_string_appendc(str, ", ");
			}
			str = write_expr_to_string(str, arg);
		}
		str = gb_string_appendc(str, ")");
	case_end;

	case_ast_node(pt, ProcType, node);
		str = gb_string_appendc(str, "proc(");
		str = write_params_to_string(str, pt->params, ", ");
		str = gb_string_appendc(str, ")");
	case_end;

	case_ast_node(st, StructType, node);
		str = gb_string_appendc(str, "struct ");
		if (st->is_packed)  str = gb_string_appendc(str, "#packed ");
		if (st->is_ordered) str = gb_string_appendc(str, "#ordered ");
		for_array(i, st->decls) {
			if (i > 0) {
				str = gb_string_appendc(str, "; ");
			}
			str = write_expr_to_string(str, st->decls[i]);
		}
		// str = write_params_to_string(str, st->decl_list, ", ");
		str = gb_string_appendc(str, "}");
	case_end;

	case_ast_node(st, RawUnionType, node);
		str = gb_string_appendc(str, "raw_union {");
		for_array(i, st->decls) {
			if (i > 0) {
				str = gb_string_appendc(str, "; ");
			}
			str = write_expr_to_string(str, st->decls[i]);
		}
		// str = write_params_to_string(str, st->decl_list, ", ");
		str = gb_string_appendc(str, "}");
	case_end;

	case_ast_node(st, UnionType, node);
		str = gb_string_appendc(str, "union {");
		for_array(i, st->decls) {
			if (i > 0) {
				str = gb_string_appendc(str, "; ");
			}
			str = write_expr_to_string(str, st->decls[i]);
		}
		// str = write_params_to_string(str, st->decl_list, ", ");
		str = gb_string_appendc(str, "}");
	case_end;

	case_ast_node(et, EnumType, node);
		str = gb_string_appendc(str, "enum ");
		if (et->base_type != NULL) {
			str = write_expr_to_string(str, et->base_type);
			str = gb_string_appendc(str, " ");
		}
		str = gb_string_appendc(str, "{");
		str = gb_string_appendc(str, "}");
	case_end;
	}

	return str;
}

gbString expr_to_string(AstNode *expression) {
	return write_expr_to_string(gb_string_make(heap_allocator(), ""), expression);
}
