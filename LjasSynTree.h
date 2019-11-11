#ifndef __LJAS_SYNTREE__
#define __LJAS_SYNTREE__
// This file was automatically generated by EbnfStudio; don't modify it!

#include <LjTools/LjasTokenType.h>
#include <LjTools/LjasToken.h>
#include <QList>

namespace Ljas {

	struct SynTree {
		enum ParserRule {
			R_First = TT_Max + 1,
			R_ADD_,
			R_CALLT_,
			R_CALL_,
			R_CAT_,
			R_DIV_,
			R_FNEW_,
			R_FORI_,
			R_FORL_,
			R_GGET_,
			R_GSET_,
			R_ISEQ_,
			R_ISFC_,
			R_ISF_,
			R_ISGE_,
			R_ISGT_,
			R_ISLE_,
			R_ISLT_,
			R_ISNE_,
			R_ISTC_,
			R_IST_,
			R_JMP_,
			R_KNIL_,
			R_KSET_,
			R_LEN_,
			R_LOOP_,
			R_LjAsm,
			R_MOD_,
			R_MOV_,
			R_MUL_,
			R_NOT_,
			R_POW_,
			R_RET_,
			R_SUB_,
			R_TDUP_,
			R_TGET_,
			R_TNEW_,
			R_TSET_,
			R_UCLO_,
			R_UGET_,
			R_UNM_,
			R_USET_,
			R_cname,
			R_comment_,
			R_const_decls,
			R_const_val,
			R_desig,
			R_fname,
			R_formal_params,
			R_function_body,
			R_function_decl,
			R_function_header,
			R_integer,
			R_label,
			R_labelDef,
			R_number,
			R_primitive,
			R_record,
			R_statement,
			R_table_literal,
			R_var_decl,
			R_var_decls,
			R_vname,
			R_Last
		};
		SynTree(quint16 r = Tok_Invalid, const Token& = Token() );
		SynTree(const Token& t ):d_tok(t){}
        ~SynTree() { foreach(SynTree* n, d_children) delete n; }

		static const char* rToStr( quint16 r );

		Ljas::Token d_tok;
		QList<SynTree*> d_children;
	};

}
#endif // __LJAS_SYNTREE__
