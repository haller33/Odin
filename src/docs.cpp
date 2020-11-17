// Generates Documentation


gb_global int print_entity_kind_ordering[Entity_Count] = {
	/*Invalid*/     -1,
	/*Constant*/    0,
	/*Variable*/    1,
	/*TypeName*/    4,
	/*Procedure*/   2,
	/*ProcGroup*/   3,
	/*Builtin*/     -1,
	/*ImportName*/  -1,
	/*LibraryName*/ -1,
	/*Nil*/         -1,
	/*Label*/       -1,
};
gb_global char const *print_entity_names[Entity_Count] = {
	/*Invalid*/     "",
	/*Constant*/    "constants",
	/*Variable*/    "variables",
	/*TypeName*/    "types",
	/*Procedure*/   "procedures",
	/*ProcGroup*/   "proc_group",
	/*Builtin*/     "",
	/*ImportName*/  "import names",
	/*LibraryName*/ "library names",
	/*Nil*/         "",
	/*Label*/       "",
};


GB_COMPARE_PROC(cmp_entities_for_printing) {
	GB_ASSERT(a != nullptr);
	GB_ASSERT(b != nullptr);
	Entity *x = *cast(Entity **)a;
	Entity *y = *cast(Entity **)b;
	int res = 0;
	res = string_compare(x->pkg->name, y->pkg->name);
	if (res != 0) {
		return res;
	}
	int ox = print_entity_kind_ordering[x->kind];
	int oy = print_entity_kind_ordering[y->kind];
	if (ox < oy) {
		return -1;
	} else if (ox > oy) {
		return +1;
	}
	res = string_compare(x->token.string, y->token.string);
	return res;
}

GB_COMPARE_PROC(cmp_ast_package_by_name) {
	GB_ASSERT(a != nullptr);
	GB_ASSERT(b != nullptr);
	AstPackage *x = *cast(AstPackage **)a;
	AstPackage *y = *cast(AstPackage **)b;
	return string_compare(x->name, y->name);
}


String alloc_comment_group_string(gbAllocator a, CommentGroup g) {
	isize len = 0;
	for_array(i, g.list) {
		String comment = g.list[i].string;
		len += comment.len;
		len += 1; // for \n
	}
	if (len == 0) {
		return make_string(nullptr, 0);
	}

	u8 *text = gb_alloc_array(a, u8, len+1);
	len = 0;
	for_array(i, g.list) {
		String comment = g.list[i].string;
		if (comment[1] == '/') {
			comment.text += 2;
			comment.len  -= 2;
		} else if (comment[1] == '*') {
			comment.text += 2;
			comment.len  -= 4;
		}
		comment = string_trim_whitespace(comment);
		gb_memmove(text+len, comment.text, comment.len);
		len += comment.len;
		text[len++] = '\n';
	}
	return make_string(text, len);
}


void print_doc_line(i32 indent, char const *fmt, ...) {
	while (indent --> 0) {
		gb_printf("\t");
	}
	va_list va;
	va_start(va, fmt);
	gb_printf_va(fmt, va);
	va_end(va);
	gb_printf("\n");
}
void print_doc_line_no_newline(i32 indent, char const *fmt, ...) {
	while (indent --> 0) {
		gb_printf("\t");
	}
	va_list va;
	va_start(va, fmt);
	gb_printf_va(fmt, va);
	va_end(va);
}

bool print_doc_comment_group_string(i32 indent, CommentGroup *g) {
	if (g == nullptr) {
		return false;
	}
	isize len = 0;
	for_array(i, g->list) {
		String comment = g->list[i].string;
		len += comment.len;
		len += 1; // for \n
	}
	if (len <= g->list.count) {
		return false;
	}

	isize count = 0;
	for_array(i, g->list) {
		String comment = g->list[i].string;
		if (comment[1] == '/') {
			comment.text += 2;
			comment.len  -= 2;
		} else if (comment[1] == '*') {
			comment.text += 2;
			comment.len  -= 4;
		}
		if (comment.len > 0 && comment[0] == ' ') {
			comment.text += 1;
			comment.len  -= 1;
		}

		if (string_starts_with(comment, str_lit("@("))) {
			continue;
		}

		print_doc_line(indent, "%.*s", LIT(comment));
		count += 1;
	}
	return count > 0;
}




void print_doc_expr(Ast *expr) {
	gbString s = nullptr;
	if (build_context.cmd_doc_flags & CmdDocFlag_All) {
		s = expr_to_string(expr);
	} else {
		s = expr_to_string_shorthand(expr);
	}
	gb_file_write(gb_file_get_standard(gbFileStandard_Output), s, gb_string_length(s));
	gb_string_free(s);
}


void print_doc_package(CheckerInfo *info, AstPackage *pkg) {
	if (pkg == nullptr) {
		return;
	}

	print_doc_line(0, "package %.*s", LIT(pkg->name));


	for_array(i, pkg->files) {
		AstFile *f = pkg->files[i];
		if (f->pkg_decl) {
			GB_ASSERT(f->pkg_decl->kind == Ast_PackageDecl);
			print_doc_comment_group_string(1, f->pkg_decl->PackageDecl.docs);
		}
	}

	if (pkg->scope != nullptr) {
		auto entities = array_make<Entity *>(heap_allocator(), 0, pkg->scope->elements.entries.count);
		defer (array_free(&entities));
		for_array(i, pkg->scope->elements.entries) {
			Entity *e = pkg->scope->elements.entries[i].value;
			switch (e->kind) {
			case Entity_Invalid:
			case Entity_Builtin:
			case Entity_Nil:
			case Entity_Label:
				continue;
			case Entity_Constant:
			case Entity_Variable:
			case Entity_TypeName:
			case Entity_Procedure:
			case Entity_ProcGroup:
			case Entity_ImportName:
			case Entity_LibraryName:
				// Fine
				break;
			}
			array_add(&entities, e);
		}
		gb_sort_array(entities.data, entities.count, cmp_entities_for_printing);

		AstPackage *curr_pkg = nullptr;
		EntityKind curr_entity_kind = Entity_Invalid;
		for_array(i, entities) {
			Entity *e = entities[i];
			if (e->pkg != pkg) {
				continue;
			}
			if (!is_entity_exported(e)) {
				continue;
			}

			if (curr_entity_kind != e->kind) {
				curr_entity_kind = e->kind;
				print_doc_line(0, "");
				print_doc_line(1, "%s", print_entity_names[e->kind]);
			}

			Ast *type_expr = nullptr;
			Ast *init_expr = nullptr;
			Ast *decl_node = nullptr;
			CommentGroup *comment = nullptr;
			CommentGroup *docs = nullptr;
			if (e->decl_info != nullptr) {
				type_expr = e->decl_info->type_expr;
				init_expr = e->decl_info->init_expr;
				decl_node = e->decl_info->decl_node;
				comment = e->decl_info->comment;
				docs = e->decl_info->docs;
			}
			GB_ASSERT(type_expr != nullptr || init_expr != nullptr);
			print_doc_line_no_newline(2, "%.*s", LIT(e->token.string));
			if (type_expr != nullptr) {
				gbString t = expr_to_string(type_expr);
				gb_printf(": %s ", t);
				gb_string_free(t);
			} else {
				gb_printf(" :");
			}
			if (e->kind == Entity_Variable) {
				if (init_expr != nullptr) {
					gb_printf("= ");
					print_doc_expr(init_expr);
				}
			} else {
				gb_printf(": ");
				print_doc_expr(init_expr);
			}

			gb_printf(";\n");


			if (build_context.cmd_doc_flags & CmdDocFlag_All) {
				if (comment) {
					// gb_printf(" <comment>");
				}
				if (print_doc_comment_group_string(3, docs)) {
					gb_printf("\n");
				}
			}
		}
		print_doc_line(0, "");
	}

	if (pkg->fullpath.len != 0) {
		print_doc_line(0, "");
		print_doc_line(1, "fullpath: %.*s", LIT(pkg->fullpath));
		print_doc_line(1, "files:");
		for_array(i, pkg->files) {
			AstFile *f = pkg->files[i];
			String filename = remove_directory_from_path(f->fullpath);
			print_doc_line(2, "%.*s", LIT(filename));
		}
	}

}

void generate_documentation(Checker *c) {
	CheckerInfo *info = &c->info;

	if (build_context.doc_packages.count != 0) {
		auto pkgs = array_make<AstPackage *>(permanent_allocator(), 0, info->packages.entries.count);
		bool was_error = false;
		for_array(j, build_context.doc_packages) {
			bool found = false;
			String name = build_context.doc_packages[j];
			for_array(i, info->packages.entries) {
				AstPackage *pkg = info->packages.entries[i].value;
				if (name == pkg->name) {
					found = true;
					array_add(&pkgs, pkg);
					break;
				}
			}
			if (!found) {
				gb_printf_err("Unknown package %.*s\n", LIT(name));
				was_error = true;
			}
		}
		if (was_error) {
			gb_exit(1);
			return;
		}

		gb_sort_array(pkgs.data, pkgs.count, cmp_ast_package_by_name);

		for_array(i, pkgs) {
			print_doc_package(info, pkgs[i]);
		}
	} else if (build_context.cmd_doc_flags & CmdDocFlag_AllPackages) {
		auto pkgs = array_make<AstPackage *>(permanent_allocator(), 0, info->packages.entries.count);
		for_array(i, info->packages.entries) {
			AstPackage *pkg = info->packages.entries[i].value;
			array_add(&pkgs, pkg);
		}

		gb_sort_array(pkgs.data, pkgs.count, cmp_ast_package_by_name);

		for_array(i, pkgs) {
			print_doc_package(info, pkgs[i]);
		}
	} else {
		GB_ASSERT(info->init_scope->flags & ScopeFlag_Pkg);
		AstPackage *pkg = info->init_scope->pkg;
		print_doc_package(info, pkg);

	}
}
