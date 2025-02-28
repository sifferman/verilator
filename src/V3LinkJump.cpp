// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Replace return/continue with jumps
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2003-2025 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
// V3LinkJump's Transformations:
//
// Each module:
//   Look for BEGINs
//      BEGIN(VAR...) -> VAR ... {renamed}
//   FOR -> WHILEs
//
//   Add JumpLabel which branches to after statements within JumpLabel
//      RETURN -> JUMPBLOCK(statements with RETURN changed to JUMPGO, ..., JUMPLABEL)
//      WHILE(... BREAK) -> JUMPBLOCK(WHILE(... statements with BREAK changed to JUMPGO),
//                                    ... JUMPLABEL)
//      WHILE(... CONTINUE) -> WHILE(JUMPBLOCK(... statements with CONTINUE changed to JUMPGO,
//                                    ... JUMPPABEL))
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3LinkJump.h"

#include "V3AstUserAllocator.h"
#include "V3Error.h"

#include <vector>

VL_DEFINE_DEBUG_FUNCTIONS;

//######################################################################

class LinkJumpVisitor final : public VNVisitor {
    // NODE STATE
    //  AstNode::user1()    -> AstJumpLabel*, for this block if endOfIter
    //  AstNode::user2()    -> AstJumpLabel*, for this block if !endOfIter
    //  AstNodeBlock::user3()  -> bool, true if contains a fork
    const VNUser1InUse m_user1InUse;
    const VNUser2InUse m_user2InUse;
    const VNUser3InUse m_user3InUse;

    // STATE
    AstNodeModule* m_modp = nullptr;  // Current module
    AstNodeFTask* m_ftaskp = nullptr;  // Current function/task
    AstNode* m_loopp = nullptr;  // Current loop
    bool m_loopInc = false;  // In loop increment
    bool m_inFork = false;  // Under fork
    int m_modRepeatNum = 0;  // Repeat counter
    VOptionBool m_unrollFull;  // Pragma full, disable, or default unrolling
    std::vector<AstNodeBlock*> m_blockStack;  // All begin blocks above current node

    // METHODS
    AstJumpLabel* findAddLabel(AstNode* nodep, bool endOfIter) {
        // Put label under given node, and if WHILE optionally at end of iteration
        UINFO(4, "Create label for " << nodep << endl);
        if (VN_IS(nodep, JumpLabel)) return VN_AS(nodep, JumpLabel);  // Done

        // Made it previously?  We always jump to the end, so this works out
        if (endOfIter) {
            if (nodep->user1p()) return VN_AS(nodep->user1p(), JumpLabel);
        } else {
            if (nodep->user2p()) return VN_AS(nodep->user2p(), JumpLabel);
        }

        AstNode* underp = nullptr;
        bool under_and_next = true;
        if (VN_IS(nodep, NodeBlock)) {
            underp = VN_AS(nodep, NodeBlock)->stmtsp();
        } else if (VN_IS(nodep, NodeFTask)) {
            underp = VN_AS(nodep, NodeFTask)->stmtsp();
        } else if (VN_IS(nodep, Foreach)) {
            if (endOfIter) {
                underp = VN_AS(nodep, Foreach)->stmtsp();
            } else {
                underp = nodep;
                under_and_next = false;  // IE we skip the entire foreach
            }
        } else if (VN_IS(nodep, While)) {
            if (endOfIter) {
                // Note we jump to end of bodysp; a FOR loop has its
                // increment under incsp() which we don't skip
                underp = VN_AS(nodep, While)->stmtsp();
            } else {
                underp = nodep;
                under_and_next = false;  // IE we skip the entire while
            }
        } else if (AstDoWhile* const dowhilep = VN_CAST(nodep, DoWhile)) {
            // Handle it the same as AstWhile, because it will be converted to it
            if (endOfIter) {
                underp = dowhilep->stmtsp();
            } else {
                underp = nodep;
                under_and_next = false;
            }
        } else {
            nodep->v3fatalSrc("Unknown jump point for break/disable/continue");
            return nullptr;
        }
        // Skip over variables as we'll just move them in a moment
        // Also this would otherwise prevent us from using a label twice
        // see t_func_return test.
        while (underp && VN_IS(underp, Var)) underp = underp->nextp();
        UASSERT_OBJ(underp, nodep, "Break/disable/continue not under expected statement");
        UINFO(5, "  Underpoint is " << underp << endl);

        if (VN_IS(underp, JumpLabel)) {
            return VN_AS(underp, JumpLabel);
        } else {  // Move underp stuff to be under a new label
            AstJumpBlock* const blockp = new AstJumpBlock{nodep->fileline(), nullptr};
            AstJumpLabel* const labelp = new AstJumpLabel{nodep->fileline(), blockp};
            blockp->labelp(labelp);

            VNRelinker repHandle;
            if (under_and_next) {
                underp->unlinkFrBackWithNext(&repHandle);
            } else {
                underp->unlinkFrBack(&repHandle);
            }
            repHandle.relink(blockp);

            blockp->addStmtsp(underp);
            // Keep any AstVars under the function not under the new JumpLabel
            for (AstNode *nextp, *varp = underp; varp; varp = nextp) {
                nextp = varp->nextp();
                if (VN_IS(varp, Var)) blockp->addHereThisAsNext(varp->unlinkFrBack());
            }
            // Label goes last
            blockp->addEndStmtsp(labelp);
            if (endOfIter) {
                nodep->user1p(labelp);
            } else {
                nodep->user2p(labelp);
            }
            return labelp;
        }
    }
    void addPrefixToBlocksRecurse(const std::string& prefix, AstNode* const nodep) {
        // Add a prefix to blocks
        // Used to not have blocks with duplicated names
        if (AstBegin* const beginp = VN_CAST(nodep, Begin)) {
            if (beginp->name() != "") beginp->name(prefix + beginp->name());
        }

        if (nodep->op1p()) addPrefixToBlocksRecurse(prefix, nodep->op1p());
        if (nodep->op2p()) addPrefixToBlocksRecurse(prefix, nodep->op2p());
        if (nodep->op3p()) addPrefixToBlocksRecurse(prefix, nodep->op3p());
        if (nodep->op4p()) addPrefixToBlocksRecurse(prefix, nodep->op4p());
        if (nodep->nextp()) addPrefixToBlocksRecurse(prefix, nodep->nextp());
    }

    // VISITORS
    void visit(AstNodeModule* nodep) override {
        if (nodep->dead()) return;
        VL_RESTORER(m_modp);
        VL_RESTORER(m_modRepeatNum);
        m_modp = nodep;
        m_modRepeatNum = 0;
        iterateChildren(nodep);
    }
    void visit(AstNodeFTask* nodep) override {
        VL_RESTORER(m_ftaskp);
        m_ftaskp = nodep;
        iterateChildren(nodep);
    }
    void visit(AstNodeBlock* nodep) override {
        UINFO(8, "  " << nodep << endl);
        VL_RESTORER(m_inFork);
        VL_RESTORER(m_unrollFull);
        m_blockStack.push_back(nodep);
        {
            if (VN_IS(nodep, Fork)) {
                m_inFork = true;  // And remains set for children
                // Mark all upper blocks also, can stop once see
                // one set to avoid O(n^2)
                for (auto itr : vlstd::reverse_view(m_blockStack)) {
                    if (itr->user3()) break;
                    itr->user3(true);
                }
            }
            nodep->user3(m_inFork);
            iterateChildren(nodep);
        }
        m_blockStack.pop_back();
    }
    void visit(AstPragma* nodep) override {
        if (nodep->pragType() == VPragmaType::UNROLL_DISABLE) {
            m_unrollFull = VOptionBool::OPT_FALSE;
            VL_DO_DANGLING(pushDeletep(nodep->unlinkFrBack()), nodep);
        } else if (nodep->pragType() == VPragmaType::UNROLL_FULL) {
            m_unrollFull = VOptionBool::OPT_TRUE;
            VL_DO_DANGLING(pushDeletep(nodep->unlinkFrBack()), nodep);
        } else {
            iterateChildren(nodep);
        }
    }
    void visit(AstRepeat* nodep) override {
        // So later optimizations don't need to deal with them,
        //    REPEAT(count,body) -> loop=count,WHILE(loop>0) { body, loop-- }
        // Note var can be signed or unsigned based on original number.
        AstNodeExpr* const countp = nodep->countp()->unlinkFrBackWithNext();
        const string name = "__Vrepeat"s + cvtToStr(m_modRepeatNum++);
        AstBegin* const beginp = new AstBegin{nodep->fileline(), "", nullptr, false, true};
        // Spec says value is integral, if negative is ignored
        AstVar* const varp
            = new AstVar{nodep->fileline(), VVarType::BLOCKTEMP, name, nodep->findSigned32DType()};
        varp->lifetime(VLifetime::AUTOMATIC);
        varp->usedLoopIdx(true);
        beginp->addStmtsp(varp);
        AstNode* initsp = new AstAssign{
            nodep->fileline(), new AstVarRef{nodep->fileline(), varp, VAccess::WRITE}, countp};
        AstNode* const decp = new AstAssign{
            nodep->fileline(), new AstVarRef{nodep->fileline(), varp, VAccess::WRITE},
            new AstSub{nodep->fileline(), new AstVarRef{nodep->fileline(), varp, VAccess::READ},
                       new AstConst{nodep->fileline(), 1}}};
        AstNodeExpr* const zerosp = new AstConst{nodep->fileline(), AstConst::Signed32{}, 0};
        AstNodeExpr* const condp = new AstGtS{
            nodep->fileline(), new AstVarRef{nodep->fileline(), varp, VAccess::READ}, zerosp};
        AstNode* const bodysp = nodep->stmtsp();
        if (bodysp) bodysp->unlinkFrBackWithNext();
        AstWhile* const whilep = new AstWhile{nodep->fileline(), condp, bodysp, decp};
        if (!m_unrollFull.isDefault()) whilep->unrollFull(m_unrollFull);
        m_unrollFull = VOptionBool::OPT_DEFAULT_FALSE;
        beginp->addStmtsp(initsp);
        beginp->addStmtsp(whilep);
        nodep->replaceWith(beginp);
        VL_DO_DANGLING(nodep->deleteTree(), nodep);
    }
    void visit(AstWhile* nodep) override {
        // Don't need to track AstRepeat/AstFor as they have already been converted
        if (!m_unrollFull.isDefault()) nodep->unrollFull(m_unrollFull);
        if (m_modp->hasParameterList() || m_modp->hasGParam())
            nodep->fileline()->modifyWarnOff(V3ErrorCode::UNUSEDLOOP, true);
        m_unrollFull = VOptionBool::OPT_DEFAULT_FALSE;
        VL_RESTORER(m_loopp);
        VL_RESTORER(m_loopInc);
        m_loopp = nodep;
        m_loopInc = false;
        iterateAndNextNull(nodep->precondsp());
        iterateAndNextNull(nodep->condp());
        iterateAndNextNull(nodep->stmtsp());
        m_loopInc = true;
        iterateAndNextNull(nodep->incsp());
    }
    void visit(AstDoWhile* nodep) override {
        // It is converted to AstWhile in this visit method
        VL_RESTORER(m_loopp);
        {
            m_loopp = nodep;
            iterateAndNextNull(nodep->condp());
            iterateAndNextNull(nodep->stmtsp());
        }
        AstNodeExpr* const condp = nodep->condp() ? nodep->condp()->unlinkFrBack() : nullptr;
        AstNode* const bodyp = nodep->stmtsp() ? nodep->stmtsp()->unlinkFrBack() : nullptr;
        AstWhile* const whilep = new AstWhile{nodep->fileline(), condp, bodyp};
        if (!m_unrollFull.isDefault()) whilep->unrollFull(m_unrollFull);
        m_unrollFull = VOptionBool::OPT_DEFAULT_FALSE;
        // No unused warning for converted AstDoWhile, as body always executes once
        nodep->fileline()->modifyWarnOff(V3ErrorCode::UNUSEDLOOP, true);
        nodep->replaceWith(whilep);
        VL_DO_DANGLING(nodep->deleteTree(), nodep);
        if (bodyp) {
            AstNode* const copiedBodyp = bodyp->cloneTree(false);
            addPrefixToBlocksRecurse("__Vdo_while1_", copiedBodyp);
            addPrefixToBlocksRecurse("__Vdo_while2_", bodyp);
            whilep->addHereThisAsNext(copiedBodyp);
        }
    }
    void visit(AstNodeForeach* nodep) override {
        VL_RESTORER(m_loopp);
        m_loopp = nodep;
        iterateAndNextNull(nodep->stmtsp());
    }
    void visit(AstReturn* nodep) override {
        iterateChildren(nodep);
        const AstFunc* const funcp = VN_CAST(m_ftaskp, Func);
        if (m_inFork) {
            nodep->v3error("Return isn't legal under fork (IEEE 1800-2023 9.2.3)");
            VL_DO_DANGLING(pushDeletep(nodep->unlinkFrBack()), nodep);
            return;
        } else if (!m_ftaskp) {
            nodep->v3error("Return isn't underneath a task or function");
        } else if (funcp && !nodep->lhsp() && !funcp->isConstructor()) {
            nodep->v3error("Return underneath a function should have return value");
        } else if (!funcp && nodep->lhsp()) {
            nodep->v3error("Return underneath a task shouldn't have return value");
        } else {
            if (funcp && nodep->lhsp()) {
                // Set output variable to return value
                nodep->addHereThisAsNext(new AstAssign{
                    nodep->fileline(),
                    new AstVarRef{nodep->fileline(), VN_AS(funcp->fvarp(), Var), VAccess::WRITE},
                    nodep->lhsp()->unlinkFrBackWithNext()});
            }
            // Jump to the end of the function call
            AstJumpLabel* const labelp = findAddLabel(m_ftaskp, false);
            nodep->addHereThisAsNext(new AstJumpGo{nodep->fileline(), labelp});
        }
        nodep->unlinkFrBack();
        VL_DO_DANGLING(pushDeletep(nodep), nodep);
    }
    void visit(AstBreak* nodep) override {
        iterateChildren(nodep);
        if (!m_loopp) {
            nodep->v3error("break isn't underneath a loop");
        } else {
            // Jump to the end of the loop
            AstJumpLabel* const labelp = findAddLabel(m_loopp, false);
            nodep->addNextHere(new AstJumpGo{nodep->fileline(), labelp});
        }
        nodep->unlinkFrBack();
        VL_DO_DANGLING(pushDeletep(nodep), nodep);
    }
    void visit(AstContinue* nodep) override {
        iterateChildren(nodep);
        if (!m_loopp) {
            nodep->v3error("continue isn't underneath a loop");
        } else {
            // Jump to the end of this iteration
            // If a "for" loop then need to still do the post-loop increment
            AstJumpLabel* const labelp = findAddLabel(m_loopp, true);
            nodep->addNextHere(new AstJumpGo{nodep->fileline(), labelp});
        }
        nodep->unlinkFrBack();
        VL_DO_DANGLING(pushDeletep(nodep), nodep);
    }
    void visit(AstDisable* nodep) override {
        UINFO(8, "   DISABLE " << nodep << endl);
        iterateChildren(nodep);
        AstNodeBlock* blockp = nullptr;
        for (AstNodeBlock* const stackp : vlstd::reverse_view(m_blockStack)) {
            UINFO(9, "    UNDERBLK  " << stackp << endl);
            if (stackp->name() == nodep->name()) {
                blockp = stackp;
                break;
            }
        }
        // if (debug() >= 9) { UINFO(0, "\n"); blockp->dumpTree("-  labeli: "); }
        if (!blockp) {
            nodep->v3warn(E_UNSUPPORTED,
                          "disable isn't underneath a begin with name: " << nodep->prettyNameQ());
        } else if (AstBegin* const beginp = VN_CAST(blockp, Begin)) {
            if (beginp->user3()) {
                nodep->v3warn(E_UNSUPPORTED, "Unsupported: disabling block that contains a fork");
            } else {
                // Jump to the end of the named block
                AstJumpLabel* const labelp = findAddLabel(beginp, false);
                nodep->addNextHere(new AstJumpGo{nodep->fileline(), labelp});
            }
        } else {
            nodep->v3warn(E_UNSUPPORTED, "Unsupported: disabling fork by name");
        }
        nodep->unlinkFrBack();
        VL_DO_DANGLING(pushDeletep(nodep), nodep);
        // if (debug() >= 9) { UINFO(0, "\n"); beginp->dumpTree("-  labelo: "); }
    }
    void visit(AstVarRef* nodep) override {
        if (m_loopInc && nodep->varp()) nodep->varp()->usedLoopIdx(true);
    }
    void visit(AstConst*) override {}
    void visit(AstNode* nodep) override { iterateChildren(nodep); }

public:
    // CONSTRUCTORS
    explicit LinkJumpVisitor(AstNetlist* nodep) { iterate(nodep); }
    ~LinkJumpVisitor() override = default;
};

//######################################################################
// Task class functions

void V3LinkJump::linkJump(AstNetlist* nodep) {
    UINFO(2, __FUNCTION__ << ": " << endl);
    { LinkJumpVisitor{nodep}; }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("linkjump", 0, dumpTreeEitherLevel() >= 3);
}
