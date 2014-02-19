/*
    This file is part of kdev-python, the python language plugin for KDevelop
    Copyright (C) 2012  Sven Brauch svenbrauch@googlemail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <language/duchain/topducontext.h>
#include <language/duchain/dumpchain.h>
#include <language/codegen/coderepresentation.h>
#include <tests/autotestshell.h>
#include <tests/testcore.h>
#include <language/duchain/duchain.h>
#include <QtTest/QtTest>
#include <KStandardDirs>
#include <language/duchain/types/functiontype.h>
#include <language/duchain/aliasdeclaration.h>

#include "parsesession.h"
#include "pythoneditorintegrator.h"
#include "declarationbuilder.h"
#include "usebuilder.h"
#include "astdefaultvisitor.h"
#include "expressionvisitor.h"
#include "contextbuilder.h"
#include "astbuilder.h"

#include "types/variablelengthcontainer.h"
#include "duchain/helpers.h"

#include "pyasttest.h"
#include "../astbuilder.h"

using namespace Python;

QTEST_MAIN(PyAstTest)

PyAstTest::PyAstTest(QObject* parent): QObject(parent)
{
    initShell();
}

void PyAstTest::initShell()
{
    AutoTestShell::init();
    TestCore* core = new TestCore();
    core->initialize(KDevelop::Core::NoUi);
    DUChain::self()->disablePersistentStorage();
    KDevelop::CodeRepresentation::setDiskChangesForbidden(true);
}

CodeAst::Ptr PyAstTest::getAst(QString code)
{
    QSharedPointer<AstBuilder> builder(new AstBuilder);
    CodeAst::Ptr result = builder->parse(KUrl("<empty>"), code);
    return result;
}

class VerifyVisitor : public AstDefaultVisitor {
public:
    VerifyVisitor() : AstDefaultVisitor(), m_nodecount(0) { };
    virtual void visitNode(Ast* node) {
        m_nodecount += 1;
        QVERIFY(! node || node->astType < Ast::LastAstType);
        AstDefaultVisitor::visitNode(node);
    };
    virtual void visitName(NameAst* node) {
        QVERIFY(! node->identifier->value.isNull());
        AstDefaultVisitor::visitName(node);
    };
    virtual void visitCode(CodeAst* node) {
        AstDefaultVisitor::visitCode(node);
        qDebug() << "done, nodes visited:" << m_nodecount;
    };
    int m_nodecount;
};

void PyAstTest::testCode(QString code)
{
    CodeAst::Ptr ast = getAst(code);
    VerifyVisitor v;
    v.visitCode(ast.data());
}

void PyAstTest::testExceptionHandlers()
{
    QString code = "try: pass\n"
                   "except FooBar as baz: pass\n"
                   "except Cat as baz: pass\n"
                   "except Dawg as asdf: pass\n";
    CodeAst::Ptr ast = getAst(code);
    VerifyVisitor v;
    v.visitCode(ast.data());
    QCOMPARE(ast->body.size(), 1);
    QVERIFY(ast->body.first()->astType == Ast::TryAstType);
    TryAst* try_ = static_cast<TryAst*>(ast->body.first());
    QCOMPARE(try_->handlers.size(), 3);
    foreach ( ExceptionHandlerAst* handler, try_->handlers ) {
        QVERIFY(handler->name);
        QCOMPARE(handler->name->astType, Ast::IdentifierAstType);
    }
}

void PyAstTest::testStatements()
{
    QFETCH(QString, code);
    testCode(code);
}

void PyAstTest::testStatements_data()
{
    QTest::addColumn<QString>("code");
    QTest::newRow("assign_int") << "a = 3";
    QTest::newRow("funcdef") << "def myfun(): pass";
    QTest::newRow("funcdef_args") << "def myfun(arg1, arg2): pass";
    QTest::newRow("funcdef_vararg") << "def myfun(arg1, *arg): pass";
    QTest::newRow("funcdef_kwarg") << "def myfun(**arg): pass";
    QTest::newRow("classdef_inheritance") << "class myclass(parent): pass";
    QTest::newRow("return") << "return 3";
    QTest::newRow("for") << "for i in 1, 2, 3: pass";
    QTest::newRow("while") << "while True: pass";
    QTest::newRow("if") << "if True: pass";
    QTest::newRow("ifElse") << "if True: pass\nelse:pass";
    QTest::newRow("with") << "with x as y: pass";
    QTest::newRow("raise") << "raise Exception";
    QTest::newRow("tryexcept") << "try:pass\nexcept:pass";
    QTest::newRow("tryexceptfinally") << "try:pass\nexcept:pass\nfinally:pass";
    QTest::newRow("assert") << "assert false";
    QTest::newRow("import") << "import foobar";
    QTest::newRow("importfrom") << "from foobar import bazbang";
    QTest::newRow("global") << "global x";
    QTest::newRow("break") << "while True: break";
    QTest::newRow("continue") << "while True: continue";
    QTest::newRow("pass") << "pass";
    QTest::newRow("nonlocal") << "nonlocal x";
}

void PyAstTest::testSlices()
{
    QFETCH(QString, code);
    testCode(code);
}

void PyAstTest::testSlices_data()
{
    QTest::addColumn<QString>("code");
    QTest::newRow("slice1") << "x[1]";
    QTest::newRow("slice2") << "x[2:3]";
    QTest::newRow("slice3") << "x[::]";
    QTest::newRow("slice4") << "x[1:2:3]";
}

void PyAstTest::testOther()
{
    QFETCH(QString, code);
    testCode(code);
}

void PyAstTest::testOther_data()
{
    QTest::addColumn<QString>("code");
    QTest::newRow("args") << "foo(bar, baz, *bang, **baa)";
    QTest::newRow("kws") << "foo(bar=baz, bang=3)";
    QTest::newRow("excpthandler") << "try:pass\nexcept foo as bar: pass";
    QTest::newRow("alias") << "import foo as bar";
}

void PyAstTest::testExpressions()
{
    QFETCH(QString, code);
    testCode(code);
}

void PyAstTest::testExpressions_data()
{
    QTest::addColumn<QString>("code");
    
    QTest::newRow("boolop") << "b or c";
    QTest::newRow("binop") << "b ^ c";
    QTest::newRow("unop") << "not a";
    QTest::newRow("lambda") << "lambda x: y";
    QTest::newRow("ifexpr") << "3 if 4 else 5";
    QTest::newRow("dict") << "{}";
    QTest::newRow("set") << "(3, 5)";
    QTest::newRow("listcomp") << "[x for x in y]";
    QTest::newRow("setcomp") << "(x for x in y)";
    QTest::newRow("dictcomp") << "{x:y for x, y in z}";
    QTest::newRow("comp") << "x < y";
    QTest::newRow("number") << "3";
    QTest::newRow("string") << "\"foo\"";
    QTest::newRow("bytes") << "b\"bytes\"";
    QTest::newRow("yield") << "yield x";
    QTest::newRow("name") << "foo";
    QTest::newRow("call") << "foo()";
    QTest::newRow("attribute") << "foo.bar";
    QTest::newRow("subscript") << "foo[3]";
    QTest::newRow("starred") << "*[1, 2, 3 ,4]";
    QTest::newRow("list") << "[]";
    QTest::newRow("tuple") << "()";
    QTest::newRow("None") << "None";
    QTest::newRow("False") << "False";
    QTest::newRow("True") << "True";
}

Q_DECLARE_METATYPE(SimpleRange);

void PyAstTest::testCorrectedFuncRanges()
{
    QFETCH(QString, code);
    QFETCH(SimpleRange, range);

    CodeAst::Ptr ast = getAst(code);
    QVERIFY(ast);
    foreach ( Ast* node, ast->body ) {
        if ( node->astType != Ast::FunctionDefinitionAstType ) {
            continue;
        }
        FunctionDefinitionAst* func = static_cast<FunctionDefinitionAst*>(node);
        QVERIFY(func->name);
        kDebug() << func->name->range() << range;
        QCOMPARE(func->name->range(), range);
    }
}

void PyAstTest::testCorrectedFuncRanges_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<SimpleRange>("range");

    QTest::newRow("decorator") << "@decorate\ndef func(arg): pass" << SimpleRange(1, 4, 1, 7);
    QTest::newRow("decorator_arg") << "@decorate(yomama=3)\ndef func(arg): pass" << SimpleRange(1, 4, 1, 7);
    QTest::newRow("two_decorators") << "@decorate2\n@decorate\ndef func(arg): pass" << SimpleRange(2, 4, 2, 7);
    QTest::newRow("decorate_class") << "class foo:\n @decorate2\n @decorate\n def func(arg): pass" << SimpleRange(3, 5, 3, 8);
}

void PyAstTest::testNewPython3()
{
    QFETCH(QString, code);
    testCode(code);
}

void PyAstTest::testNewPython3_data()
{
    QTest::addColumn<QString>("code");
    QTest::newRow("funcannotation1") << "def haul(item: Haulable, *vargs: PackAnimal) -> Distance: pass";
    QTest::newRow("funcannotation2") << "def foo() -> expr: pass";
    QTest::newRow("funcannotation3") << "def foo(a: 'x', b: 5 + 6, c: list) -> max(2, 9): pass";
    QTest::newRow("kwonly1") << "def compare(a, b, *, key=None): pass";
    QTest::newRow("kwonly2") << "def foo(a: 'x', b: 5 + 6, c: list) -> max(2, 9): pass";
    QTest::newRow("listunpack") << "(a, *rest, b) = range(5)";
    QTest::newRow("metaclass") << "class C(metaclass=M): pass";
    QTest::newRow("exception_chain") << "raise SecondaryException() from primary_exception";
    QTest::newRow("yield_from") << "def foo(): yield from [1, 2, 3]";
}

void PyAstTest::testClass()
{
    testCode("class c: pass");
}
