use std::collections::HashSet;
use std::fs::{read_to_string, OpenOptions};
use std::io::Write;
use std::path::PathBuf;

use itertools::Itertools;
use rustc_hir::def::{DefKind, Res};
use rustc_hir::def_id::DefId;
use rustc_hir::intravisit::{walk_expr, walk_impl_item, walk_item, Visitor};
use rustc_hir::{Expr, ExprKind, ItemKind};
use rustc_middle::hir::nested_filter::OnlyBodies;
use rustc_middle::ty::{TyCtxt, TyKind};

use log::warn;

pub struct Analyzer<'tcx> {
    /// The type context.
    pub tcx: TyCtxt<'tcx>,

    /// The output file path.
    output: PathBuf,

    /// Set of foreign functions.
    functions: HashSet<String>,
}

impl<'tcx> Analyzer<'tcx> {
    pub fn new(tcx: TyCtxt<'tcx>) -> Self {
        let output = PathBuf::from(std::env::var("BFFF_OUTPUT").unwrap());

        let mut functions = HashSet::new();

        for function in read_to_string(&output).unwrap().lines() {
            functions.insert(function.into());
        }

        Self {
            tcx,
            output,
            functions,
        }
    }

    pub fn finalize(&mut self) {
        let contents = self.functions.iter().join("\n");

        let mut file = OpenOptions::new()
            .write(true)
            .truncate(true)
            .open(&self.output)
            .unwrap();

        file.write_all(contents.as_bytes()).unwrap();
    }

    fn canonical_path(&self, def_id: DefId) -> String {
        let relative_path = self.tcx.def_path_str(def_id);

        // Get the *full* path, including the local crate name
        let full_path = if def_id.is_local() {
            // It's a local DefId, so we need to add the crate name
            let crate_name = self.tcx.crate_name(def_id.krate).to_string();

            if relative_path.is_empty() {
                // This handles the edge case of the crate root itself
                crate_name
            } else {
                format!("{}::{}", crate_name, relative_path)
            }
        } else {
            // It's external, so def_path_str already included the crate name
            relative_path
        };

        full_path
    }

    fn visit_expr_method_call(&mut self, hir_id: rustc_hir::HirId) -> () {
        let typeck_results = self.tcx.typeck(hir_id.owner.def_id);

		match typeck_results.type_dependent_def(hir_id) {
			Some((def_kind, def_id)) => match def_kind {
				DefKind::AssocFn => {
					let crate_name = self.tcx.crate_name(def_id.krate).to_string();

					if crate_name == "libc" || crate_name == "libloading" {
						return;
					}

					let fn_name = self.tcx.item_name(def_id).to_string();

					// Get the method type
					let method_ty = self.tcx.type_of(def_id).skip_binder();

					match method_ty.kind() {
						TyKind::FnDef(..) => {
							if self.tcx.is_foreign_item(def_id) {
								self.functions.insert(fn_name);
							}
						}

						_ => warn!("Method wasn't an 'FnDef'"),
					}
				}

				_ => todo!(),
			}
			_ => warn!("Function cannot be typed"),
		}
    }

    fn visit_expr_call(&mut self, fun: &Expr) {
        let owner_id = fun.hir_id.owner;
        let owner_def_kind = self.tcx.def_kind(owner_id);

        if !owner_def_kind.is_fn_like() {
            return;
        }

        let local_def_id = owner_id.to_def_id().as_local().unwrap();

        if let ExprKind::Path(qpath) = fun.kind {
            let typeck_results = self.tcx.typeck(local_def_id);

            if let Res::Def(def_kind, def_id) = typeck_results.qpath_res(&qpath, fun.hir_id) {
                if def_kind == DefKind::Fn {
					let crate_name = self.tcx.crate_name(def_id.krate).to_string();

					if crate_name == "libc" || crate_name == "libloading" {
						return;
					}

					let fn_name = self.tcx.item_name(def_id).to_string();

                    match typeck_results.expr_ty_opt(fun) {
                        Some(ty) => match ty.kind() {
                            TyKind::FnDef(..) => {
                                if self.tcx.is_foreign_item(def_id) {
                                    self.functions.insert(fn_name);
                                }
                            }

                            _ => warn!("Function wasn't an 'FnDef'"),
                        },

                        _ => warn!("Function cannot be typed"),
                    }
                }
            }
        }
    }
}

impl<'tcx> Visitor<'tcx> for Analyzer<'tcx> {
    type NestedFilter = OnlyBodies;

    fn maybe_tcx(&mut self) -> Self::MaybeTyCtxt {
        self.tcx
    }

    fn visit_expr(&mut self, expr: &'tcx Expr<'tcx>) -> Self::Result {
        match expr.kind {
            ExprKind::MethodCall(_, _, _, _) => self.visit_expr_method_call(expr.hir_id),
            ExprKind::Call(fun, _) => self.visit_expr_call(fun),
            _ => walk_expr(self, expr),
        }
    }

    fn visit_item(&mut self, item: &'tcx rustc_hir::Item<'tcx>) -> Self::Result {
        match item.kind {
            ItemKind::Fn { .. } => walk_item(self, item),
            _ => {}
        };
    }

    fn visit_impl_item(&mut self, impl_item: &'tcx rustc_hir::ImplItem<'tcx>) -> Self::Result {
        match impl_item.kind {
            rustc_hir::ImplItemKind::Fn(_, _) => walk_impl_item(self, impl_item),
            _ => {}
        };
    }
}
