// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Replace return/continue with jumps
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2003-2020 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
// V3Incremepts's Transformations:
//
// XXX: TODO: Write some docs
//
//*************************************************************************

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V3Increments.h"
#include "V3Ast.h"

#include <algorithm>
#include <cstdarg>
#include <vector>

#include <iostream>

//######################################################################

class IncrementsVisitor : public AstNVisitor {
public:
    // CONSTRUCTORS
    explicit IncrementsVisitor(AstNetlist* nodep) {}
    virtual ~IncrementsVisitor() {}
    virtual void visit(AstPreInc* nodep) VL_OVERRIDE {
        std::cout << "wow! me here!" << __func__ << ":" << __LINE__ << std::endl;
    }
    virtual void visit(AstNode* nodep) VL_OVERRIDE {
        std::cout << "lol! me here!" << __func__ << ":" << __LINE__ << std::endl;
        iterateChildren(nodep);
    }
};

void V3Increments::increments(AstNetlist* nodep) {
    UINFO(2,__FUNCTION__<<": "<<endl);
    {
        IncrementsVisitor bvisitor (nodep);
    }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("link", 0, v3Global.opt.dumpTreeLevel(__FILE__) >= 3);
}
