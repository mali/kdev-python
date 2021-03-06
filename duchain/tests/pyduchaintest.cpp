/*****************************************************************************
 * Copyright 2010 (c) Miquel Canes Gonzalez <miquelcanes@gmail.com>          *
 * Copyright 2012 (c) Sven Brauch <svenbrauch@googlemail.com>                *
 *                                                                           *
 * Permission is hereby granted, free of charge, to any person obtaining     *
 * a copy of this software and associated documentation files (the           *
 * "Software"), to deal in the Software without restriction, including       *
 * without limitation the rights to use, copy, modify, merge, publish,       *
 * distribute, sublicense, and/or sell copies of the Software, and to        *
 * permit persons to whom the Software is furnished to do so, subject to     *
 * the following conditions:                                                 *
 *                                                                           *
 * The above copyright notice and this permission notice shall be            *
 * included in all copies or substantial portions of the Software.           *
 *                                                                           *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,           *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF        *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                     *
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE    *
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION    *
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION     *
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.           *
 *****************************************************************************/

#include "pyduchaintest.h"

#include <stdlib.h>

#include <language/duchain/topducontext.h>
#include <language/codegen/coderepresentation.h>
#include <tests/autotestshell.h>
#include <tests/testcore.h>
#include <language/duchain/duchain.h>
#include <QtTest/QtTest>
#include <KStandardDirs>
#include <QtGui/QApplication>
#include <language/duchain/types/functiontype.h>
#include <language/duchain/types/containertypes.h>
#include <language/duchain/aliasdeclaration.h>
#include <language/backgroundparser/backgroundparser.h>
#include <language/interfaces/iastcontainer.h>
#include <interfaces/ilanguagecontroller.h>
#include <tests/testfile.h>

#include "parsesession.h"
#include "pythoneditorintegrator.h"
#include "declarationbuilder.h"
#include "usebuilder.h"
#include "astdefaultvisitor.h"
#include "expressionvisitor.h"
#include "contextbuilder.h"
#include "astbuilder.h"

#include "duchain/helpers.h"

QTEST_MAIN(PyDUChainTest)

using namespace KDevelop;
using namespace Python;


PyDUChainTest::PyDUChainTest(QObject* parent): QObject(parent)
{
    assetsDir = QDir(DUCHAIN_PY_DATA_DIR);
    if (!assetsDir.cd("data")) {
        qFatal("Failed find data directory for test files. Aborting");
    }

    char tempdirname[] = "/tmp/kdev-python-test.XXXXXX";
    if (!mkdtemp(tempdirname)) {
        qFatal("Failed to create temp directory, Aboring");
    }
    testDir = QDir(QString(tempdirname));
    kDebug() << "tempdirname" << tempdirname;

    QByteArray pythonpath = qgetenv("PYTHONPATH");
    pythonpath.prepend(":").prepend(assetsDir.absolutePath().toAscii());
    qputenv("PYTHONPATH", pythonpath);

    initShell();
}

QList<QString> PyDUChainTest::FindPyFiles(QDir& rootDir)
{
    QList<QString> foundfiles;
    rootDir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
    rootDir.setNameFilters(QStringList() << "*.py"); // We only want .py files
    
    QDirIterator it(rootDir, QDirIterator::Subdirectories);
    while(it.hasNext()) {
        foundfiles.append(it.next());
    }
    return foundfiles;
}

void PyDUChainTest::init()
{
    QString currentTest = QString(QTest::currentTestFunction());
    if (lastTest == currentTest) {
        kDebug() << "Already prepared assets for " << currentTest << ", skipping";
        return;
    } else {
        lastTest = currentTest;
    }
    kDebug() << "Preparing assets for test " << currentTest;
    
    QDir assetModuleDir = QDir(assetsDir.absolutePath());
    
    if (!assetModuleDir.cd(currentTest)) {
        kDebug() << "Asset directory " << currentTest
                 <<  " does not exist under " << assetModuleDir.absolutePath() << ". Skipping it.";
        return;
    }
    
    kDebug() << "Searching for python files in " << assetModuleDir.absolutePath();
    
    QList<QString> foundfiles = FindPyFiles(assetModuleDir);

    QString correctionFileDir = KStandardDirs::locate("data", "kdevpythonsupport/correction_files/");
    KUrl correctionFileUrl = KUrl(correctionFileDir + "testCorrectionFiles/example.py");
    correctionFileUrl.cleanPath();
    foundfiles.prepend(correctionFileUrl.path());

    for ( int i = 0; i < 2; i++ ) {
        // Parse each file twice, to ensure no parsing-order related bugs appear.
        // Such bugs will need separate unit tests and should not influence these.
        foreach(const QString filename, foundfiles) {
            kDebug() << "Parsing asset: " << filename;
            DUChain::self()->updateContextForUrl(IndexedString(filename), KDevelop::TopDUContext::AllDeclarationsContextsAndUses);
            ICore::self()->languageController()->backgroundParser()->parseDocuments();
        }
        foreach(const QString filename, foundfiles) {
            DUChain::self()->waitForUpdate(IndexedString(filename), KDevelop::TopDUContext::AllDeclarationsContextsAndUses);
        }
        while ( ICore::self()->languageController()->backgroundParser()->queuedCount() > 0 ) {
            // make sure to wait for all parsejobs to finish
            QTest::qWait(10);
        }
    }
}
    

void PyDUChainTest::initShell()
{
    AutoTestShell::init();
    TestCore* core = new TestCore();
    core->initialize(KDevelop::Core::NoUi);
    
    KUrl doc_url = KUrl(KStandardDirs::locate("data", "kdevpythonsupport/documentation_files/builtindocumentation.py"));
    doc_url.cleanPath(KUrl::SimplifyDirSeparators);
    
    kDebug() << doc_url;

    DUChain::self()->updateContextForUrl(IndexedString(doc_url), KDevelop::TopDUContext::AllDeclarationsContextsAndUses);
    ICore::self()->languageController()->backgroundParser()->parseDocuments();
    DUChain::self()->waitForUpdate(IndexedString(doc_url), KDevelop::TopDUContext::AllDeclarationsContextsAndUses);
    
    DUChain::self()->disablePersistentStorage();
    KDevelop::CodeRepresentation::setDiskChangesForbidden(true);
}

ReferencedTopDUContext PyDUChainTest::parse(const QString& code)
{
    TestFile* testfile = new TestFile(code + "\n", "py", 0, testDir.absolutePath().append("/"));
    createdFiles << testfile;

    testfile->parse((TopDUContext::Features) (TopDUContext::ForceUpdate | TopDUContext::AST) );
    testfile->waitForParsed(500);
    
    if ( testfile->isReady() ) {
        m_ast = static_cast<Python::ParseSession*>(testfile->topContext()->ast().data())->ast;
        return testfile->topContext();
    }
    else Q_ASSERT(false && "Timed out waiting for parser results, aborting all tests");
    return 0;
}

PyDUChainTest::~PyDUChainTest()
{
    foreach ( TestFile* f, createdFiles ) {
        delete f;
    }
    testDir.rmdir(testDir.absolutePath());
}

void PyDUChainTest::testMultiFromImport()
{
    QFETCH(QString, code);
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    DUChainReadLocker lock;
    QList<Declaration*> a = ctx->findDeclarations(QualifiedIdentifier("a"));
    QList<Declaration*> b = ctx->findDeclarations(QualifiedIdentifier("b"));
    QVERIFY(! a.isEmpty());
    QVERIFY(! b.isEmpty());
    QVERIFY(a.first()->abstractType()->toString().endsWith("int"));
    QVERIFY(b.first()->abstractType()->toString().endsWith("int"));
}

void PyDUChainTest::testMultiFromImport_data() {
    QTest::addColumn<QString>("code");
    QTest::newRow("multiimport") << "import testMultiFromImport.i.localvar1\n"
                                    "import testMultiFromImport.i.localvar2\n"
                                    "a = testMultiFromImport.i.localvar1\n"
                                    "b = testMultiFromImport.i.localvar2\n";
}

void PyDUChainTest::testRelativeImport()
{
    QFETCH(QString, code);
    QFETCH(QString, token);
    QFETCH(QString, type);
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    DUChainReadLocker lock;
    QList<Declaration*> t = ctx->findDeclarations(QualifiedIdentifier(token));
    QVERIFY(! t.isEmpty());
    QVERIFY(t.first()->abstractType()->toString().endsWith(type));
}

void PyDUChainTest::testRelativeImport_data() {
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("token");
    QTest::addColumn<QString>("type");
    QTest::newRow(".local") << "from testRelativeImport.m.sm1.go import i1" << "i1" << "int";
    QTest::newRow(".init") << "from testRelativeImport.m.sm1.go import i2" << "i2" << "int";
    QTest::newRow("..local") << "from testRelativeImport.m.sm1.go import i3" << "i3" << "int";
    QTest::newRow("..init") << "from testRelativeImport.m.sm1.go import i4" << "i4" << "int";
    QTest::newRow("..sub.local") << "from testRelativeImport.m.sm1.go import i5" << "i5" << "int";
    QTest::newRow("..sub.init") << "from testRelativeImport.m.sm1.go import i6" << "i6" << "int";
}

void PyDUChainTest::testCrashes() {
    QFETCH(QString, code);
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    QVERIFY(m_ast);
    QVERIFY(! m_ast->body.isEmpty());
}

void PyDUChainTest::testCrashes_data() {
    QTest::addColumn<QString>("code");
    
    QTest::newRow("unicode_char") << "a = \"í\"";
    QTest::newRow("unicode escape char") << "print(\"\\xe9\")";
    QTest::newRow("augassign") << "a = 3\na += 5";
    QTest::newRow("delete") << "a = 3\ndel a";
    QTest::newRow("for_else") << "for i in range(3): pass\nelse: pass";
    QTest::newRow("for_while") << "while i < 4: pass\nelse: pass";
    QTest::newRow("ellipsis") << "a[...]";
    QTest::newRow("tuple_assign_unknown") << "foo = (unknown, unknown, unknown)";
    QTest::newRow("for_assign_unknown") << "for foo, bar, baz in unknown: pass";
    QTest::newRow("negative slice index") << "t = (1, 2, 3)\nd = t[-1]";
    QTest::newRow("decorator_with_args") << "@foo('bar', 'baz')\ndef myfunc(): pass";
    QTest::newRow("non_name_decorator") << "@foo.crazy_decorators\ndef myfunc(): pass";
    QTest::newRow("static_method") << "class c:\n @staticmethod\n def method(): pass";
    QTest::newRow("vararg_in_middle") << "def func(a, *b, c): pass\nfunc(1, 2, 3, 4, 5)";
    QTest::newRow("return_outside_function") << "return 3";
    QTest::newRow("stacked_lambdas") << "l4 = lambda x = lambda y = lambda z=1 : z : y() : x()";
    QTest::newRow("fancy generator context range") << "c1_list = sorted(letter for (letter, meanings) \\\n"
               "in ambiguous_nucleotide_values.iteritems() \\\n"
               "if set([codon[0] for codon in codons]).issuperset(set(meanings)))";
    QTest::newRow("fancy class range") << "class SchemeLexer(RegexLexer):\n"
                                          "  valid_name = r'[a-zA-Z0-9!$%&*+,/:<=>?@^_~|-]+'\n"
                                          "\n"
                                          "  tokens = {\n"
                                          "      'root' : [\n"
                                          "          # the comments - always starting with semicolon\n"
                                          "          # and going to the end of the line\n"
                                          "          (r';.*$', Comment.Single),\n"
                                          "\n"
                                          "          # whitespaces - usually not relevant\n"
                                          "          (r'\\s+', Text),\n"
                                          "\n"
                                          "          # numbers\n"
                                          "          (r'-?\\d+\\.\\d+', Number.Float),\n"
                                          "          (r'-?\\d+', Number.Integer)\n"
                                          "      ],\n"
                                          "  }\n";
    QTest::newRow("another fancy range") << "setup_args['data_files'] = [\n"
                                          "     (os.path.dirname(os.path.join(install_base_dir, pattern)),\n"
                                          "     [ f for f in glob.glob(pattern) ])\n"
                                          "        for pattern in patterns\n"
                                          "]\n";
    QTest::newRow("kwarg_empty_crash") << "def myfun(): return\ncheckme = myfun(kw=something)";
    QTest::newRow("stacked_tuple_hang") << "tree = (1,(2,(3,(4,(5,'Foo')))))";
    QTest::newRow("stacked_tuple_hang2") << "tree = (257,"
         "(264,"
          "(285,"
           "(259,"
            "(272,"
              "(275,"
               "(1, 'return')))))))";
    QTest::newRow("very_large_tuple_hang") << "tree = "
        "(257,"
         "(264,"
          "(285,"
           "(259,"
            "(1, 'def'),"
            "(1, 'f'),"
            "(260, (7, '('), (8, ')')),"
            "(11, ':'),"
            "(291,"
             "(4, ''),"
             "(5, ''),"
             "(264,"
              "(265,"
               "(266,"
                "(272,"
                 "(275,"
                  "(1, 'return'),"
                  "(313,"
                   "(292,"
                    "(293,"
                     "(294,"
                      "(295,"
                       "(297,"
                        "(298,"
                         "(299,"
                          "(300,"
                           "(301,"
                            "(302, (303, (304, (305, (2, '1')))))))))))))))))),"
               "(264,"
                "(265,"
                 "(266,"
                  "(272,"
                   "(276,"
                   "(1, 'yield'),"
                    "(313,"
                     "(292,"
                      "(293,"
                       "(294,"
                        "(295,"
                         "(297,"
                          "(298,"
                           "(299,"
                            "(300,"
                             "(301,"
                              "(302,"
                               "(303, (304, (305, (2, '1')))))))))))))))))),"
                 "(4, ''))),"
               "(6, ''))))),"
           "(4, ''),"
           "(0, ''))))";
    QTest::newRow("attribute_hang") << "s = \"123\"\n"
            "s = s.replace(u'ł', 'l').\\\n"
            "replace(u'ó', 'o').\\\n"
            "replace(u'ą', 'a').\\\n"
            "replace(u'ę', 'e').\\\n"
            "replace(u'ś', 's').\\\n"
            "replace(u'ż', 'z').\\\n"
            "replace(u'ź', 'z').\\\n"
            "replace(u'ć', 'c').\\\n"
            "replace(u'ń', 'n').\\\n"
            "replace(u'б', 'b').\\\n"
            "replace(u'в', 'v').\\\n"
            "replace(u'г', 'g').\\\n"
            "replace(u'д', 'd').\\\n"
            "replace(u'ё', 'yo').\\\n"
            "replace(u'ć', 'c').\\\n"
            "replace(u'ń', 'n').\\\n"
            "replace(u'б', 'b').\\\n"
            "replace(u'в', 'v').\\\n"
            "replace(u'г', 'g').\\\n"
            "replace(u'д', 'd').\\\n"
            "replace(u'ё', 'yo').\\\n"
            "replace(u'ć', 'c').\\\n"
            "replace(u'ń', 'n').\\\n"
            "replace(u'б', 'b').\\\n"
            "replace(u'в', 'v').\\\n"
            "replace(u'г', 'g').\\\n"
            "replace(u'д', 'd').\\\n"
            "replace(u'ё', 'yo')\n";
    QTest::newRow("function context range crash") << "def myfunc(arg):\n foo = 3 + \\\n[x for x in range(20)]";
    QTest::newRow("decorator comprehension crash") << "@implementer_only(interfaces.ISSLTransport,\n"
                 "                   *[i for i in implementedBy(tcp.Client)\n"
                 "                     if i != interfaces.ITLSTransport])\n"
                 "class Client(tcp.Client):\n"
                 "  pass\n";
    QTest::newRow("comprehension_as_default_crash") << "def foo(bar = [item for (_, item) in items()]):\n return";
    QTest::newRow("try_except") << "try: pass\nexcept: pass";
    QTest::newRow("try_except_type") << "try: pass\nexcept FooException: pass";
    QTest::newRow("try_except_type_as") << "try: pass\nexcept FooException as bar: pass";
    QTest::newRow("import_missing") << "from this_does_not_exist import nor_does_this";

    QTest::newRow("list_append_missing") << "foo = []\nfoo.append(missing)";
    QTest::newRow("list_append_missing_arg") << "foo = []\nfoo.append()";

    QTest::newRow("list_extend_missing") << "foo = []\nfoo.extend(missing)";
    QTest::newRow("list_extend_missing_arg") << "foo = []\nfoo.extend()";
}

void PyDUChainTest::testClassVariables()
{
    ReferencedTopDUContext ctx = parse("class c():\n myvar = 3;\n def meth(self):\n  print(myvar)");
    QVERIFY(ctx.data());
    DUChainWriteLocker lock(DUChain::lock());
    CursorInRevision relevantPosition(3, 10);
    DUContext* c = ctx->findContextAt(relevantPosition);
    QVERIFY(c);
    int useIndex = c->findUseAt(relevantPosition);
    if ( useIndex != -1 ) {
        QVERIFY(useIndex < c->usesCount());
        const Use* u = &(c->uses()[useIndex]);
        QVERIFY(not u->usedDeclaration(c->topContext()));
    }
}

void PyDUChainTest::testWarnNewNotCls()
{
    QFETCH(QString, code);
    QFETCH(int, probs);

    ReferencedTopDUContext ctx = parse(code);
    DUChainReadLocker lock;
    QCOMPARE(ctx->problems().count(), probs);
}

void PyDUChainTest::testWarnNewNotCls_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<int>("probs");

    QTest::newRow("check_for_new_first_arg_cls") << "class c():\n def __new__(clf, other):\n  pass" << 1;
    QTest::newRow("check_for_new_first_arg_cls_0") << "class c():\n def __new__(cls, other):\n  pass" << 0;
    QTest::newRow("check_first_arg_class_self") << "class c():\n def test(seff, masik):\n  pass" << 1;
    QTest::newRow("check_first_arg_class_self_0") << "class c():\n def test(self, masik):\n  pass" << 0;
}

void PyDUChainTest::testBinaryOperatorsUnsure()
{
    QFETCH(QString, code);
    QFETCH(QString, type);

    ReferencedTopDUContext ctx = parse(code);
    DUChainWriteLocker lock;
    QList<Declaration*> ds = ctx->findDeclarations(QualifiedIdentifier("checkme"));
    QVERIFY(!ds.isEmpty());
    Declaration* d = ds.first();
    QVERIFY(d);
    QVERIFY(d->abstractType());
    QCOMPARE(d->abstractType()->toString(), type);
}

void PyDUChainTest::testBinaryOperatorsUnsure_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("type");

    QTest::newRow("check_unsure_type_0") << "class c():\n def __mul__(self, other):\n  return int();\nx = c();\nx = 3.5;\ny = 3;\ncheckme = x * y;" << "unsure (float, int)";
    QTest::newRow("check_unsure_type_1") << "class c():\n def __mul__(self, other):\n  return int();\nx = c();\nx = 3;\ny = 3;\ncheckme = x * y;" << "int";
    QTest::newRow("check_unsure_type_2") << "class c():\n pass;\nx = c();\nx = 3;\ny = 3;\ncheckme = x * y;" << "int";
    QTest::newRow("check_unsure_type_3") << "class c():\n pass;\nclass d():\n pass;\nx = c();\nx = d();\ny = 3;\ncheckme = x * y;" << "int";
}


void PyDUChainTest::testFlickering()
{
    QFETCH(QStringList, code);
    QFETCH(int, before);
    QFETCH(int, after);
    
    TestFile f(code[0], "py");
    f.parse(TopDUContext::ForceUpdate);
    f.waitForParsed(500);
    
    ReferencedTopDUContext ctx = f.topContext();
    QVERIFY(ctx);
    
    DUChainWriteLocker lock(DUChain::lock());
    int count = ctx->localDeclarations().size();
    qDebug() << "Declaration count before: " << count;
    QVERIFY(count == before);
    lock.unlock();
    
    f.setFileContents(code[1]);
    f.parse(TopDUContext::ForceUpdate);
    f.waitForParsed(500);
    ctx = f.topContext();
    QVERIFY(ctx);
    
    lock.lock();
    count = ctx->localDeclarations().size();
    qDebug() << "Declaration count afterwards: " << count;
    QVERIFY(count == after);
    
    foreach(Declaration* dec, ctx->localDeclarations()) {
        qDebug() << dec->toString() << dec->range();
        qDebug() << dec->uses().size();
    }
}

void PyDUChainTest::testFlickering_data()
{
    QTest::addColumn<QStringList>("code");
    QTest::addColumn<int>("before");
    QTest::addColumn<int>("after");
    
    QTest::newRow("declaration_flicker") << ( QStringList() << "a=2\n" << "b=3\na=2\n" ) << 1 << 2;
}

void PyDUChainTest::testCannotOverwriteBuiltins()
{
    QFETCH(QString, code);
    QFETCH(QString, expectedType);

    ReferencedTopDUContext ctx = parse(code);
    DUChainWriteLocker lock;
    QList<Declaration*> ds = ctx->findDeclarations(QualifiedIdentifier("checkme"));
    QVERIFY(!ds.isEmpty());
    Declaration* d = ds.first();
    QVERIFY(d);
    QVERIFY(d->abstractType());
    QEXPECT_FAIL("can_have_custom", "not implemented", Continue);
    QEXPECT_FAIL("can_have_custom3", "not implemented", Continue);
    QCOMPARE(d->abstractType()->toString(), expectedType);
}

void PyDUChainTest::testCannotOverwriteBuiltins_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("expectedType");

    QTest::newRow("list_assign") << "class list(): pass\ncheckme = []\ncheckme.append(3)" << "list of int";
    QTest::newRow("str_assign") << "str = 5; checkme = 'Foo'" << "str";
    QTest::newRow("str_assign2") << "class Foo: pass\nstr = Foo; checkme = 'Foo'" << "str";
    QTest::newRow("str_assign3") << "from testCannotOverwriteBuiltins.i import Foo as str\ncheckme = 'Foo'" << "str";
    QTest::newRow("for") << "for str in [1, 2, 3]: pass\ncheckme = 'Foo'" << "str";
    QTest::newRow("assert") << "assert isinstance(str, int)\ncheckme = 'Foo'" << "str";
    QTest::newRow("assert2") << "assert isinstance(str, int)\ncheckme = 3" << "int";
    QTest::newRow("can_have_custom") << "from testCannotOverwriteBuiltins import mod\ncheckme = mod.open()" << "int";
    QTest::newRow("can_have_custom2") << "from testCannotOverwriteBuiltins import mod\ncheckme = open().read()" << "str";
    QTest::newRow("can_have_custom3") << "from testCannotOverwriteBuiltins import mod\ncheckme = mod.open().read()" << "mixed";
}

void PyDUChainTest::testVarKWArgs()
{
    ReferencedTopDUContext ctx = parse("def myfun(arg, *vararg, **kwarg):\n pass\n pass");
    DUChainWriteLocker lock;
    QVERIFY(ctx);
    DUContext* func = ctx->findContextAt(CursorInRevision(1, 0));
    QVERIFY(func);
    QVERIFY(! func->findDeclarations(QualifiedIdentifier("arg")).isEmpty());
    QVERIFY(! func->findDeclarations(QualifiedIdentifier("vararg")).isEmpty());
    QVERIFY(! func->findDeclarations(QualifiedIdentifier("kwarg")).isEmpty());
    QVERIFY(func->findDeclarations(QualifiedIdentifier("vararg")).first()->abstractType()->toString().startsWith("tuple"));
    QCOMPARE(func->findDeclarations(QualifiedIdentifier("kwarg")).first()->abstractType()->toString(),
             QString("dict of str : unknown"));
}

void PyDUChainTest::testSimple()
{
    QFETCH(QString, code);
    QFETCH(int, decls);
    QFETCH(int, uses);
    
    ReferencedTopDUContext ctx = parse(code);
    DUChainWriteLocker lock(DUChain::lock());
    QVERIFY(ctx);
    
    QVector< Declaration* > declarations = ctx->localDeclarations();
    
    QCOMPARE(declarations.size(), decls);
    
    int usesCount = 0;
    foreach(Declaration* d, declarations) {
        usesCount += d->uses().size();
        
        QVERIFY(!d->abstractType().isNull());
    }
    
    QCOMPARE(usesCount, uses);
}

void PyDUChainTest::testSimple_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<int>("decls");
    QTest::addColumn<int>("uses");
    
    QTest::newRow("assign") << "b = 2;" << 1 << 0;
    QTest::newRow("assign_str") << "b = 'hola';" << 1 << 0;
    QTest::newRow("op") << "a = 3; b = a+2;" << 2 << 1;
    QTest::newRow("bool") << "a = True" << 1 << 0;
    QTest::newRow("op") << "a = True and True;" << 1 << 0;
}

class AttributeRangeTestVisitor : public AstDefaultVisitor {
public:
    bool found;
    SimpleRange searchingForRange;
    QString searchingForIdentifier;
    virtual void visitAttribute(AttributeAst* node) {
        SimpleRange r(0, node->startCol, 0, node->endCol);
        qDebug() << "Found attr: " << r << node->attribute->value << ", looking for: " << searchingForRange << searchingForIdentifier;
        if ( r == searchingForRange && node->attribute->value == searchingForIdentifier ) {
            found = true;
            return;
        }
        AstDefaultVisitor::visitAttribute(node);
    }
    virtual void visitFunctionDefinition(FunctionDefinitionAst* node) {
        SimpleRange r(0, node->name->startCol, 0, node->name->endCol);
        qDebug() << "Found func: " << r << node->name->value << ", looking for: " << searchingForRange << searchingForIdentifier;
        qDebug() << node->arguments->vararg << node->arguments->kwarg;
        if ( r == searchingForRange && node->name->value == searchingForIdentifier ) {
            found = true;
            return;
        }
        if ( node->arguments->vararg ) {
            SimpleRange r(0, node->arguments->vararg->startCol, 0, node->arguments->vararg->startCol+node->arguments->vararg->argumentName->value.length());
            qDebug() << "Found vararg: " << node->arguments->vararg->argumentName->value << r;
            if ( r == searchingForRange && node->arguments->vararg->argumentName->value == searchingForIdentifier ) {
                found = true;
                return;
            }
        }
        if ( node->arguments->kwarg ) {
            SimpleRange r(0, node->arguments->kwarg->startCol, 0, node->arguments->kwarg->startCol+node->arguments->kwarg->argumentName->value.length());
            qDebug() << "Found kwarg: " << node->arguments->kwarg->argumentName->value << r;
            if ( r == searchingForRange && node->arguments->kwarg->argumentName->value == searchingForIdentifier ) {
                found = true;
                return;
            }
        }
        AstDefaultVisitor::visitFunctionDefinition(node);
    }
    virtual void visitClassDefinition(ClassDefinitionAst* node) {
        SimpleRange r(0, node->name->startCol, 0, node->name->endCol);
        qDebug() << "Found cls: " << r << node->name->value << ", looking for: " << searchingForRange << searchingForIdentifier;
        if ( r == searchingForRange && node->name->value == searchingForIdentifier ) {
            found = true;
            return;
        }
        AstDefaultVisitor::visitClassDefinition(node);
    }
    virtual void visitImport(ImportAst* node) {
        foreach ( const AliasAst* name, node->names ) {
            if ( name->name ) {
                qDebug() << "found import" << name->name->value << name->name->range();
            }
            if ( name->name && name->name->value == searchingForIdentifier && name->name->range() == searchingForRange ) {
                found = true;
                return;
            }
            if ( name->asName ) {
                qDebug() << "found import" << name->asName->value << name->asName->range();
            }
            if ( name->asName && name->asName->value == searchingForIdentifier && name->asName->range() == searchingForRange ) {
                found = true;
                return;
            }
        }
    }
};

void PyDUChainTest::testRanges()
{
    QFETCH(QString, code);
    QFETCH(int, expected_amount_of_variables); Q_UNUSED(expected_amount_of_variables);
    QFETCH(QStringList, column_ranges);
    
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    
    QVERIFY(m_ast);
    
    for ( int i = 0; i < column_ranges.length(); i++ ) {
        int scol = column_ranges.at(i).split(",")[0].toInt();
        int ecol = column_ranges.at(i).split(",")[1].toInt();
        QString identifier = column_ranges.at(i).split(",")[2];
        SimpleRange r(0, scol, 0, ecol);
        
        AttributeRangeTestVisitor* visitor = new AttributeRangeTestVisitor();
        visitor->searchingForRange = r;
        visitor->searchingForIdentifier = identifier;
        visitor->visitCode(m_ast.data());
        QCOMPARE(visitor->found, true);
        delete visitor;
    }
}

void PyDUChainTest::testRanges_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<int>("expected_amount_of_variables");
    QTest::addColumn<QStringList>("column_ranges");
    
    QTest::newRow("attr_two_attributes") << "base.attr" << 2 << ( QStringList() << "5,8,attr" );
    QTest::newRow("attr_three_attributes") << "base.attr.subattr" << 3 << ( QStringList() << "5,8,attr" << "10,16,subattr" );
    QTest::newRow("attr_functionCall") << "base.attr().subattr" << 3 << ( QStringList() << "5,8,attr" << "12,18,subattr" );
    QTest::newRow("attr_stringSubscript") << "base.attr[\"a.b.c..de\"].subattr" << 3 << ( QStringList() << "5,8,attr" << "23,29,subattr" );
    QTest::newRow("attr_functionCallWithArguments") << "base.attr(arg1, arg2).subattr" << 5 << ( QStringList() << "5,8,attr" << "22,28,subattr" );
    QTest::newRow("attr_functionCallWithArgument_withInner") << "base.attr(arg1.parg2).subattr" << 5 << ( QStringList() << "5,8,attr" << "22,28,subattr" << "15,19,parg2" );
    
    QTest::newRow("funcrange_def") << "def func(): pass" << 1 << ( QStringList() << "4,7,func" );
    QTest::newRow("funcrange_spaces_def") << "def    func(): pass" << 1 << ( QStringList() << "7,10,func" );
    QTest::newRow("classdef_range") << "class cls(): pass" << 1 << ( QStringList() << "6,8,cls" );
    QTest::newRow("classdef_range_inheritance") << "class cls(parent1, parent2): pass" << 1 << ( QStringList() << "6,8,cls" );
    QTest::newRow("classdef_range_inheritance_spaces") << "class       cls(  parent1,    parent2     ):pass" << 1 << ( QStringList() << "12,14,cls" );
    QTest::newRow("vararg_kwarg") << "def func(*vararg, **kwargs): pass" << 2 << ( QStringList() << "10,16,vararg" << "20,26,kwargs" );

    QTest::newRow("import") << "import sys" << 1 << ( QStringList() << "7,10,sys" );
    QTest::newRow("import2") << "import i.localvar1" << 1 << ( QStringList() << "7,18,i.localvar1" );
    QTest::newRow("import3") << "import sys as a" << 1 << ( QStringList() << "13,14,a" );
}

class TypeTestVisitor : public AstDefaultVisitor {
public:
    QString searchingForType;
    TopDUContextPointer ctx;
    bool found;
    virtual void visitName(NameAst* node) {
        if ( node->identifier->value != "checkme" ) return;
        QList<Declaration*> decls = ctx->findDeclarations(QualifiedIdentifier(node->identifier->value));
        if ( ! decls.length() ) {
            kDebug() << "No declaration found for " << node->identifier->value;
            return;
        }
        Declaration* d = decls.last();
        kDebug() << "Declaration: " << node->identifier->value << d->type<StructureType>();
        QVERIFY(d->abstractType());
        kDebug() << "found: " << node->identifier->value << "is" << d->abstractType()->toString() << "should be" << searchingForType;
        if ( d->abstractType()->toString().replace("__kdevpythondocumentation_builtin_", "").startsWith(searchingForType) ) {
            found = true;
            return;
        }
    };
};

void PyDUChainTest::testTypes()
{
    
    QFETCH(QString, code);
    QFETCH(QString, expectedType);
    
    ReferencedTopDUContext ctx = parse(code.toAscii());
    QVERIFY(ctx);
    QVERIFY(m_ast);
    
    DUChainReadLocker lock(DUChain::lock());
    TypeTestVisitor* visitor = new TypeTestVisitor();
    visitor->ctx = TopDUContextPointer(ctx.data());
    visitor->searchingForType = expectedType;
    visitor->visitCode(m_ast.data());
    QEXPECT_FAIL("lambda", "not implemented: aliasing lambdas", Continue);
    QCOMPARE(visitor->found, true);
}

void PyDUChainTest::testTypes_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("expectedType");
    
    QTest::newRow("listtype") << "checkme = []" << "list";
    QTest::newRow("listtype_func") << "checkme = list()" << "list";
    QTest::newRow("listtype_with_contents") << "checkme = [1, 2, 3, 4, 5]" << "list of int";
    QTest::newRow("listtype_extended") << "some_misc_var = []; checkme = some_misc_var" << "list";
    QTest::newRow("dicttype") << "checkme = {}" << "dict";
    QTest::newRow("dicttype_get") << "d = {0.4:5}; checkme = d.get(0)" << "int";
    QTest::newRow("dicttype_func") << "checkme = dict()" << "dict";
    QTest::newRow("dicttype_extended") << "some_misc_var = {}; checkme = some_misc_var" << "dict";
    QTest::newRow("bool") << "checkme = True" << "bool";
    QTest::newRow("float") << "checkme = 3.7" << "float";
    QTest::newRow("int") << "checkme = 3" << "int";

    QTest::newRow("with") << "with open('foo') as f: checkme = f.read()" << "str";

    QTest::newRow("list_access_right_open_slice") << "some_list = []; checkme = some_list[2:]" << "list";
    QTest::newRow("list_access_left_open_slice") << "some_list = []; checkme = some_list[:2]" << "list";
    QTest::newRow("list_access_closed_slice") << "some_list = []; checkme = some_list[2:17]" << "list";
    QTest::newRow("list_access_step") << "some_list = []; checkme = some_list[::2]" << "list";
    QTest::newRow("list_access_singleItem") << "some_list = []; checkme = some_list[42]" << "mixed";
    
    QTest::newRow("funccall_number") << "def foo(): return 3; \ncheckme = foo();" << "int";
    QTest::newRow("funccall_string") << "def foo(): return 'a'; \ncheckme = foo();" << "str";
    QTest::newRow("funccall_list") << "def foo(): return []; \ncheckme = foo();" << "list";
    QTest::newRow("funccall_dict") << "def foo(): return {}; \ncheckme = foo();" << "dict";
    
    QTest::newRow("tuple1") << "checkme, foo = 3, \"str\"" << "int";
    QTest::newRow("tuple2") << "foo, checkme = 3, \"str\"" << "str";
    QTest::newRow("tuple2_negative_index") << "foo = (1, 2, 'foo')\ncheckme = foo[-1]" << "str";
    QTest::newRow("tuple_type") << "checkme = 1, 2" << "tuple";
    
    QTest::newRow("dict_iteritems") << "d = {1:2, 3:4}\nfor checkme, k in d.iteritems(): pass" << "int";
    QTest::newRow("enumerate_key") << "d = [str(), str()]\nfor checkme, value in enumerate(d): pass" << "int";
    QTest::newRow("enumerate_value") << "d = [str(), str()]\nfor key, checkme in enumerate(d): pass" << "str";
    QTest::newRow("dict_enumerate") << "d = {1:2, 3:4}\nfor key, checkme in enumerate(d.values()): pass" << "int";

    QTest::newRow("dict_assign_twice") << "d = dict(); d[''] = 0; d = dict(); d[''] = 0; checkme = d"
                                       << "unsure (dict of str : int, dict)";
    
    QTest::newRow("class_method_import") << "class c:\n attr = \"foo\"\n def m():\n  return attr;\n  return 3;\ni=c()\ncheckme=i.m()" << "int";
    QTest::newRow("getsListDecorator") << "foo = [1, 2, 3]\ncheckme = foo.reverse()" << "list of int";

    QTest::newRow("fromAssertIsinstance") << "class c(): pass\ncheckme = mixed()\nassert isinstance(checkme, c)\n" << "c";
    QTest::newRow("fromAssertIsinstanceInvalid") << "class c(): pass\ncheckme = mixed()\nassert isinstance(c, checkme)\n" << "mixed";
    QTest::newRow("fromAssertIsinstanceInvalid2") << "class c(): pass\ncheckme = mixed()\nassert isinstance(D, c)\n" << "mixed";
    QTest::newRow("fromAssertIsinstanceInvalid3") << "checkme = int()\nassert isinstance(checkme, X)\n" << "int";
    QTest::newRow("fromAssertIsinstanceInvalid4") << "checkme = int()\nassert isinstance(checkme)\n" << "int";
    QTest::newRow("fromAssertType") << "class c(): pass\ncheckme = mixed()\nassert type(checkme) == c\n" << "c";
    QTest::newRow("fromIfType") << "class c(): pass\ncheckme = mixed()\nif type(checkme) == c: pass\n" << "c";
    QTest::newRow("fromIfIsinstance") << "class c(): pass\ncheckme = mixed()\nif isinstance(checkme, c): pass\n" << "c";
    
    QTest::newRow("diff_local_classattr") << "class c(): attr = 1\ninst=c()\ncheckme = c.attr" << "int";
    QTest::newRow("diff_local_classattr2") << "local=3\nclass c(): attr = 1\ninst=c()\ncheckme = c.local" << "mixed";
    QTest::newRow("diff_local_classattr3") << "attr=3.5\nclass c(): attr = 1\ninst=c()\ncheckme = c.attr" << "int";
//     QTest::newRow("class_method_self") << "class c:\n def func(checkme, arg, arg2):\n  pass\n" << "c";
//    QTest::newRow("funccall_dict") << "def foo(): return foo; checkme = foo();" << (uint) IntegralType::TypeFunction;
    
    QTest::newRow("tuple_simple") << "mytuple = 3, 5.5\ncheckme, foobar = mytuple" << "int";
    QTest::newRow("tuple_simple2") << "mytuple = 3, 5.5\nfoobar, checkme = mytuple" << "float";
    QTest::newRow("tuple_simple3") << "mytuple = 3, 5.5, \"str\", 3, \"str\"\na, b, c, d, checkme = mytuple" << "str";

    QTest::newRow("if_expr_sure") << "checkme = 3 if 7 > 9 else 5" << "int";

    QTest::newRow("unary_op") << "checkme = -42" << "int";
    
    QTest::newRow("tuple_funcret") << "def myfun(): return 3, 5\ncheckme, a = myfun()" << "int";
    QTest::newRow("tuple_funcret2") << "def myfun():\n t = 3, 5\n return t\ncheckme, a = myfun()" << "int";
    
    QTest::newRow("yield") << "def myfun():\n yield 3\ncheckme = myfun()" << "list of int";
    QTest::newRow("yield_twice") << "def myfun():\n yield 3\n yield 'foo'\ncheckme = myfun()" << "list of unsure (int, str)";
    // this is mostly a check that it doesn't crash
    QTest::newRow("yield_return") << "def myfun():\n return 3\n yield 'foo'\ncheckme = myfun()" << "unsure (int, list of str)";

    QTest::newRow("lambda") << "x = lambda t: 3\ncheckme = x()" << "int";
    QTest::newRow("lambda_failure") << "x = lambda t: 3\ncheckme = t" << "mixed";

    QTest::newRow("function_arg_tuple") << "def func(*arg):\n foo, bar = arg\n return bar\ncheckme = func(3, 5)" << "int";
    QTest::newRow("function_arg_tuple2") << "def func(*arg):\n return arg[-1]\ncheckme = func(3, \"Foo\")" << "str";

    QTest::newRow("tuple_indexaccess") << "t = 3, 5.5\ncheckme = t[0]" << "int";
    QTest::newRow("tuple_indexaccess2") << "t = 3, 5.5\ncheckme = t[1]" << "float";
    QTest::newRow("tuple_indexaccess3") << "t = 3, 4\ncheckme = t[1]" << "int";

    QTest::newRow("dict_unsure") << "t = dict(); t = {3: str()}\ncheckme = t[1].capitalize()" << "str";
    QTest::newRow("unsure_attr_access") << "d = str(); d = 3; checkme = d.capitalize()" << "str";

    QTest::newRow("class_create_var") << "class c: pass\nd = c()\nd.foo = 3\ncheckme = d.foo" << "int";
    
    QTest::newRow("tuple_loop") << "t = [(1, \"str\")]\nfor checkme, a in t: pass" << "int";
    
    QTest::newRow("no_hints_type") << "def myfun(arg): arg = 3; return arg\ncheckme = myfun(3)" << "int";
    QTest::newRow("hints_type") << "def myfun(arg): return arg\ncheckme = myfun(3)" << "int";
    QTest::newRow("args_type") << "def myfun(*args): return args[0]\ncheckme = myfun(3)" << "int";
    QTest::newRow("kwarg_type") << "def myfun(**args): return args[0]\ncheckme = myfun(a=3)" << "int";
    
    QTest::newRow("tuple_listof") << "l = [(1, 2), (3, 4)]\ncheckme = l[1][0]" << "int";

    QTest::newRow("getitem") << "class c:\n def __getitem__(self, slice): return 3.14\na = c()\ncheckme = a[2]" << "float";
    
    QTest::newRow("constructor_type_deduction") << "class myclass:\n"
                                                   "\tdef __init__(self, param): self.foo=param\n"
                                                   "checkme = myclass(3).foo" << "int";
    QTest::newRow("simpe_type_deduction") << "def myfunc(arg): return arg\n"
                                             "checkme = myfunc(3)" << "int";
    QTest::newRow("functionCall_functionArg_part1") << "def getstr(): return \"foo\"\n"
                                                 "def identity(f): return f\n"
                                                 "f1 = getstr\n"
                                                 "checkme = f1()" << "str";
    QTest::newRow("functionCall_functionArg_part2") << "def getstr(): return \"foo\"\n"
                                                 "def identity(f): return f\n"
                                                 "f1 = identity(getstr)\n"
                                                 "checkme = f1()\n" << "str";
    QTest::newRow("functionCall_functionArg_full") << "def getstr(): return \"foo\"\n"
                                                 "def identity(f): return f\n"
                                                 "f1 = getstr\n"
                                                 "f2 = identity(getstr)\n"
                                                 "a = getstr()\n"
                                                 "b = f1()\n"
                                                 "checkme = f2()\n" << "str";
    QTest::newRow("vararg_before_other_args") << "def myfun(a, b, *z, x): return z[0]\n"
                                                 "checkme = myfun(False, False, 1, x = False)" << "int";
    QTest::newRow("vararg_before_other_args2") << "def myfun(a, b, *z, x): return z[3]\n"
                                                  "checkme = myfun(False, False, 1, 2, 3, \"str\", x = False)" << "str";
    QTest::newRow("vararg_constructor") << "class myclass():\n"
                                           "  def __init__(self, *arg): self.prop = arg[0]\n"
                                           "obj = myclass(3, 5); checkme = obj.prop" << "int";
    QTest::newRow("global_variable") << "a = 3\n"
                                        "def f1():\n"
                                        "  global a\n"
                                        "  return a\n"
                                        "checkme = f1()\n" << "int";
    QTest::newRow("global_variable2") << "a = 3\n"
                                        "def f1():\n"
                                        "  global a\n"
                                        "  a = \"str\"\n"
                                        "  return a\n"
                                        "checkme = f1()\n" << "str";
    QTest::newRow("global_scope_variable") << "a = 3\n"
                                        "def f1():\n"
                                        "  return a\n"
                                        "checkme = f1()\n" << "int";
    QTest::newRow("global_no_toplevel_dec") << "def f1():\n"
                                        "  global a\n  a = 3\n"
                                        "  return a\n"
                                        "checkme = f1()\n" << "int";

    QTest::newRow("top_level_vs_class_member") << "var = 3\n"
                                                  "class myclass:\n"
                                                  "  def __init__(self): self.var = \"str\"\n"
                                                  "  def f1(): return var\n"
                                                  "checkme = myclass.f1()" << "int";
}

typedef QPair<Declaration*, int> pair;

void PyDUChainTest::testImportDeclarations() {
    QFETCH(QString, code);
    QFETCH(QStringList, expectedDecls);
    QFETCH(bool, shouldBeAliased);
    
    ReferencedTopDUContext ctx = parse(code.toAscii());
    QVERIFY(ctx);
    QVERIFY(m_ast);
    
    DUChainReadLocker lock(DUChain::lock());
    foreach ( const QString& expected, expectedDecls ) {
        bool found = false;
        QString name = expected;
        QList<pair> decls = ctx->allDeclarations(CursorInRevision::invalid(), ctx->topContext(), false);
        kDebug() << "FOUND DECLARATIONS:";
        foreach ( const pair& current, decls ) {
            kDebug() << current.first->toString() << current.first->identifier().identifier().byteArray() << name;
        }
        foreach ( const pair& current, decls ) {
            if ( ! ( current.first->identifier().identifier().byteArray() == name ) ) continue;
            kDebug() << "Found: " << current.first->toString() << " for " << name;
            AliasDeclaration* isAliased = dynamic_cast<AliasDeclaration*>(current.first);
            if ( isAliased && shouldBeAliased ) {
                found = true; // TODO fixme
            }
            else if ( ! isAliased  && ! shouldBeAliased ) {
                found = true;
            }
        }
        QVERIFY(found);
    }
}

void PyDUChainTest::testProblemCount()
{
    QFETCH(QString, code);
    QFETCH(int, problemsCount);

    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);

    DUChainReadLocker lock;
    QCOMPARE(ctx->problems().size(), problemsCount);
}

void PyDUChainTest::testProblemCount_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<int>("problemsCount");

    QTest::newRow("list_comp") << "[foo for foo in range(3)]" << 0;
    QTest::newRow("list_comp_wrong") << "[bar for foo in range(3)]" << 1;
    QTest::newRow("list_comp_staticmethod") << "class A:\n @staticmethod\n def func(cls):\n"
                                        "  [a for a in [1, 2, 3]]" << 0;
    QTest::newRow("list_comp_other_decorator") << "def decorate(): pass\nclass A:\n @decorate\n def func(self):\n"
                                        "  [a for a in [1, 2, 3]]" << 0;
    QTest::newRow("list_comp_other_wrong") << "def decorate(): pass\nclass A:\n @decorate\n def func(self):\n"
                                        "  [x for a in [1, 2, 3]]" << 1;
    QTest::newRow("list_comp_staticmethod_wrong") << "class A:\n @staticmethod\n def func(cls):\n"
                                        "  [x for a in [1, 2, 3]]" << 1;
}

void PyDUChainTest::testImportDeclarations_data() {
    QTest::addColumn<QString>("code");
    QTest::addColumn<QStringList>("expectedDecls");
    QTest::addColumn<bool>("shouldBeAliased");
    
    QTest::newRow("from_import") << "from testImportDeclarations.i import checkme" << ( QStringList() << "checkme" ) << true;
    QTest::newRow("import") << "import testImportDeclarations.i" << ( QStringList() << "testImportDeclarations" ) << false;
    QTest::newRow("import_as") << "import testImportDeclarations.i as checkme" << ( QStringList() << "checkme" ) << false;
    QTest::newRow("from_import_as") << "from testImportDeclarations.i import checkme as checkme" << ( QStringList() << "checkme" ) << true;
    QTest::newRow("from_import_missing") << "from testImportDeclarations.i import missing as checkme" << ( QStringList() ) << true;
}

typedef QPair<Declaration*, int> p;

void PyDUChainTest::testAutocompletionFlickering()
{
    TestFile f("foo = 3\nfoo2 = 2\nfo", "py");
    f.parse(TopDUContext::ForceUpdate);
    f.waitForParsed(500);
    
    ReferencedTopDUContext ctx1 = f.topContext();
    DUChainWriteLocker lock(DUChain::lock());
    QVERIFY(ctx1);
    QList<p> decls1 = ctx1->allDeclarations(CursorInRevision::invalid(), ctx1->topContext());
    QList<DeclarationId> declIds;
    foreach ( p d, decls1 ) {
        declIds << d.first->id();
    }
    lock.unlock();
    
    f.setFileContents("foo = 3\nfoo2 = 2\nfoo");
    f.parse(TopDUContext::ForceUpdate);
    f.waitForParsed(500);
    
    ReferencedTopDUContext ctx2 = f.topContext();
    QVERIFY(ctx2);
    lock.lock();
    QList<p> decls2 = ctx2->allDeclarations(CursorInRevision::invalid(), ctx2->topContext());
    foreach ( p d2, decls2 ) {
        kDebug() << "@1: " << d2.first->toString() << "::" << d2.first->id().hash() << "<>" << declIds.first().hash();
        QVERIFY(d2.first->id() == declIds.first());
        declIds.removeFirst();
    }
    lock.unlock();
    
    qDebug() << "=========================";
    
    TestFile g("def func():\n\tfoo = 3\n\tfoo2 = 2\n\tfo", "py");
    g.parse(TopDUContext::ForceUpdate);
    g.waitForParsed(500);
    
    ctx1 = g.topContext();
    lock.lock();
    QVERIFY(ctx1);
    decls1 = ctx1->allDeclarations(CursorInRevision::invalid(), ctx1->topContext(), false).first().first->internalContext()
                 ->allDeclarations(CursorInRevision::invalid(), ctx1->topContext());
    declIds.clear();
    foreach ( p d, decls1 ) {
        declIds << d.first->id();
    }
    lock.unlock();
    
    g.setFileContents("def func():\n\tfoo = 3\n\tfoo2 = 2\n\tfoo");
    g.parse(TopDUContext::ForceUpdate);
    g.waitForParsed(500);
    
    ctx2 = g.topContext();
    QVERIFY(ctx2);
    lock.lock();
    decls2 = ctx2->allDeclarations(CursorInRevision::invalid(), ctx2->topContext(), false).first().first->internalContext()
                 ->allDeclarations(CursorInRevision::invalid(), ctx2->topContext());
    foreach ( p d2, decls2 ) {
        kDebug() << "@2: " << d2.first->toString() << "::" << d2.first->id().hash() << "<>" << declIds.first().hash();
        QVERIFY(d2.first->id() == declIds.first());
        declIds.removeFirst();
    }
    lock.unlock();
}

void PyDUChainTest::testFunctionHints()
{
    QFETCH(QString, code);
    QFETCH(QString, expectedType);
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    DUChainWriteLocker lock;
    QList< Declaration* > decls = ctx->findDeclarations(KDevelop::Identifier("checkme"));
    QVERIFY(! decls.isEmpty());
    Declaration* d = decls.first();
    QVERIFY(d->abstractType());
    QCOMPARE(d->abstractType()->toString(), expectedType);
}

void PyDUChainTest::testFunctionHints_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("expectedType");
    
    QTest::newRow("func_return_type") << "def myfun(arg) -> int: pass\ncheckme = myfun(\"3\")" << "unsure (void, int)";
    QTest::newRow("argument_type") << "def myfun(arg : int): return arg\ncheckme = myfun(foobar)" << "int";
    QTest::newRow("argument_type_only_if_typeof") << "def myfun(arg : 3): return arg\ncheckme = myfun(foobar)" << "mixed";
}

void PyDUChainTest::testHintedTypes()
{
    QFETCH(QString, code);
    QFETCH(QString, expectedType);
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    DUChainWriteLocker lock;
    QList< Declaration* > decls = ctx->findDeclarations(KDevelop::Identifier("checkme"));
    QVERIFY(! decls.isEmpty());
    Declaration* d = decls.first();
    QVERIFY(d->abstractType());
    QCOMPARE(d->abstractType()->toString(), expectedType);
}

void PyDUChainTest::testHintedTypes_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("expectedType");

    QTest::newRow("simple_hint") << "def myfunc(x): return x\ncheckme = myfunc(3)" << "int";
    QTest::newRow("hint_unsure") << "def myfunc(x): return x\nmyfunc(3.5)\ncheckme = myfunc(3)" << "unsure (float, int)";
    QTest::newRow("unsure_attribute") << "def myfunc(x): return x.capitalize()\nmyfunc(3.5)\ncheckme = myfunc(str())" << "str";
}

void PyDUChainTest::testDecorators()
{
    QFETCH(QString, code);
//     QFETCH(int, amountOfDecorators);
    QFETCH(QStringList, names);
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    DUChainReadLocker lock(DUChain::lock());
    Python::FunctionDeclaration* decl = dynamic_cast<Python::FunctionDeclaration*>(
        ctx->allDeclarations(CursorInRevision::invalid(), ctx->topContext()).first().first);
    QVERIFY(decl);
    foreach ( const QString& decoratorName, names ) {
        QVERIFY(Helper::findDecoratorByName<Python::FunctionDeclaration>(decl, decoratorName));
    }
}

void PyDUChainTest::testDecorators_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<int>("amountOfDecorators");
    QTest::addColumn<QStringList>("names");
    
    QTest::newRow("one_decorator") << "@foo\ndef func(): pass" << 1 << ( QStringList() << "foo" );
    QTest::newRow("decorator_with_args") << "@foo(2, \"bar\")\ndef func(): pass" << 1 << ( QStringList() << "foo");
    QTest::newRow("two_decorators") << "@foo\n@bar(17)\ndef func(): pass" << 2 << ( QStringList() << "foo" << "bar" );
}

void PyDUChainTest::testOperators()
{
    QFETCH(QString, code);
    QFETCH(QString, expectedType);
    code.prepend("from testOperators.example import *\n\n");
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);

    DUChainReadLocker lock(DUChain::lock());
    TypeTestVisitor* visitor = new TypeTestVisitor();
    visitor->ctx = TopDUContextPointer(ctx.data());
    visitor->searchingForType = expectedType;
    visitor->visitCode(m_ast.data());

    QVERIFY(visitor->found);
}

void PyDUChainTest::testOperators_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("expectedType");

    QTest::newRow("add") << "checkme = Example() + Example()" << "Add";
    QTest::newRow("sub") << "checkme = Example() - Example()" << "Sub";
    QTest::newRow("mul") << "checkme = Example() * Example()" << "Mul";
    QTest::newRow("floordiv") << "checkme = Example() // Example()" << "Floordiv";
    QTest::newRow("mod") << "checkme = Example() % Example()" << "Mod";
    QTest::newRow("pow") << "checkme = Example() ** Example()" << "Pow";
    QTest::newRow("lshift") << "checkme = Example() << Example()" << "Lshift";
    QTest::newRow("rshift") << "checkme = Example() >> Example()" << "Rshift";
    QTest::newRow("and") << "checkme = Example() & Example()" << "And";
    QTest::newRow("xor") << "checkme = Example() ^ Example()" << "Xor";
    QTest::newRow("or") << "checkme = Example() | Example()" << "Or";
}

void PyDUChainTest::testFunctionArgs()
{
    ReferencedTopDUContext ctx = parse("def ASDF(arg1, arg2):\n"
                                       "  arg1 = arg2");
    DUChainWriteLocker lock(DUChain::lock());
    QVERIFY(ctx);
    QVERIFY(m_ast);
//     dumpDUContext(ctx);

    QCOMPARE(ctx->childContexts().size(), 2);
    DUContext* funcArgCtx = ctx->childContexts().first();
    QCOMPARE(funcArgCtx->type(), DUContext::Function);
    QCOMPARE(funcArgCtx->localDeclarations().size(), 2);
    QVERIFY(!funcArgCtx->owner());
    
    Python::FunctionDeclaration* decl = dynamic_cast<Python::FunctionDeclaration*>(
                                    ctx->allDeclarations(CursorInRevision::invalid(), ctx->topContext()).first().first);
    QVERIFY(decl);
    QCOMPARE(decl->type<FunctionType>()->arguments().length(), 2);
    qDebug() << decl->type<FunctionType>()->arguments().length() << 2;

    DUContext* funcBodyCtx = ctx->childContexts().last();
    QCOMPARE(funcBodyCtx->type(), DUContext::Other);
    QVERIFY(funcBodyCtx->owner());
    QVERIFY(funcBodyCtx->localDeclarations().isEmpty());
}

void PyDUChainTest::testInheritance()
{
    QFETCH(QString, code);
    QFETCH(int, expectedBaseClasses);
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    DUChainReadLocker lock(DUChain::lock());
    QList<p> decls = ctx->allDeclarations(CursorInRevision::invalid(), ctx->topContext(), false);
    bool found = false;
    bool classDeclFound = false;
    foreach ( const p& item, decls ) {
        if ( item.first->identifier().toString() == "B" ) {
            auto klass = dynamic_cast<Python::ClassDeclaration*>(item.first);
            QVERIFY(klass);
            QCOMPARE(klass->baseClassesSize(), static_cast<unsigned int>(expectedBaseClasses));
            classDeclFound = true;
        }
        if ( item.first->identifier().toString() == "checkme" ) {
            QCOMPARE(item.first->abstractType()->toString(), QString("int"));
            found = true;
        }
    }
    QVERIFY(found);
    QVERIFY(classDeclFound);
}

void PyDUChainTest::testInheritance_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<int>("expectedBaseClasses");
    
    QTest::newRow("simple") << "class A():\n\tattr = 3\n\nclass B(A):\n\tpass\n\ninst=B()\ncheckme = inst.attr" << 1;
    QTest::newRow("context_import_prereq") << "import testInheritance.i\ninst=testInheritance.i.testclass()\n"
                                              "checkme = inst.attr\nclass B(): pass" << 1; // 1 because object
    QTest::newRow("context_import") << "import testInheritance.i\n\nclass B(testInheritance.i.testclass):\n"
                                       "\ti = 4\n\ninst=B()\ncheckme = inst.attr" << 1;
}

void PyDUChainTest::testClassContextRanges()
{
    QString code = "class my_class():\n pass\n \n \n \n \n";
    ReferencedTopDUContext ctx = parse(code);
    DUChainWriteLocker lock;
    DUContext* classContext = ctx->findContextAt(CursorInRevision(5, 0));
    QVERIFY(classContext);
    QVERIFY(classContext->type() == DUContext::Class);
}

void PyDUChainTest::testContainerTypes()
{
    QFETCH(QString, code);
    QFETCH(QString, contenttype);
    QFETCH(bool, use_type);
    ReferencedTopDUContext ctx = parse(code);
    QVERIFY(ctx);
    
    DUChainReadLocker lock(DUChain::lock());
    QList<Declaration*> decls = ctx->findDeclarations(QualifiedIdentifier("checkme"));
    QVERIFY(decls.length() > 0);
    QVERIFY(decls.first()->abstractType());
    kDebug() << "TEST type is: " << decls.first()->abstractType().unsafeData()->toString();
    if ( ! use_type ) {
        auto type = ListType::Ptr::dynamicCast(decls.first()->abstractType());
        QVERIFY(type);
        QVERIFY(type->contentType());
        QCOMPARE(type->contentType().abstractType()->toString(), contenttype);
    }
    else {
        QVERIFY(decls.first()->abstractType());
        QCOMPARE(decls.first()->abstractType()->toString(), contenttype);
    }
}

void PyDUChainTest::testContainerTypes_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("contenttype");
    QTest::addColumn<bool>("use_type");
    
    QTest::newRow("list_of_int") << "checkme = [1, 2, 3]" << "int" << false;
    QTest::newRow("list_of_int_call") << "checkme = list([1, 2, 3])" << "int" << false;
    QTest::newRow("generator") << "checkme = [i for i in [1, 2, 3]]" << "int" << false;
    QTest::newRow("list_access") << "list = [1, 2, 3]\ncheckme = list[0]" << "int" << true;
    QTest::newRow("set_of_int") << "checkme = {1, 2, 3}" << "int" << false;
    QTest::newRow("set_of_int_call") << "checkme = set({1, 2, 3})" << "int" << false;
    QTest::newRow("set_generator") << "checkme = {i for i in [1, 2, 3]}" << "int" << false;
    QTest::newRow("dict_of_int") << "checkme = {a:1, b:2, c:3}" << "int" << false;
    QTest::newRow("dict_of_int_call") << "checkme = dict({a:1, b:2, c:3})" << "int" << false;
    QTest::newRow("dict_generator") << "checkme = {\"Foo\":i for i in [1, 2, 3]}" << "int" << false;
    QTest::newRow("dict_access") << "list = {a:1, b:2, c:3}\ncheckme = list[0]" << "int" << true;
    QTest::newRow("generator_attribute") << "checkme = [item.capitalize() for item in ['foobar']]" << "str" << false;
    QTest::newRow("cannot_change_type") << "checkme = [\"Foo\", \"Bar\"]" << "str" << false;
    QTest::newRow("cannot_change_type2") << "[1, 2, 3].append(5)\ncheckme = [\"Foo\", \"Bar\"]" << "str" << false;
    
    QTest::newRow("list_append") << "d = []\nd.append(3)\ncheckme = d[0]" << "int" << true;
    QTest::newRow("list_extend") << "d = []; q = [int()]\nd.extend(q)\ncheckme = d[0]" << "int" << true;

    QTest::newRow("for_loop") << "d = [3]\nfor item in d:\n checkme = item" << "int" << true;
    QTest::newRow("for_loop_unsure") << "d = [3, \"foo\"]\nfor item in d:\n checkme = item" << "unsure (int, str)" << true;
    QTest::newRow("for_loop_tuple_1") << "d = [(3, 3.5)]\nfor a, b in d:\n checkme = a" << "int" << true;
    QTest::newRow("for_loop_tuple_2") << "d = [(3, 3.5)]\nfor a, b in d:\n checkme = b" << "float" << true;
    QTest::newRow("for_loop_tuple_unsure") << "d = [(3, 3.5), (3.5, 3)]\nfor a, b in d:\n checkme = b"
                                           << "unsure (float, int)" << true;
}

void PyDUChainTest::testVariableCreation()
{
    QFETCH(QString, code);
    QFETCH(QStringList, expected_local_declarations);
    QFETCH(QStringList, expected_types);

    ReferencedTopDUContext top = parse(code);
    QVERIFY(top);

    DUChainReadLocker lock;
    auto localDecls = top->localDeclarations();
    QVector<QString> localDeclNames;
    for ( const Declaration* d: localDecls ) {
        localDeclNames.append(d->identifier().toString());
    }
    Q_ASSERT(expected_types.size() == expected_local_declarations.size());
    int offset = 0;
    for ( const QString& expected : expected_local_declarations ) {
        int index = localDeclNames.indexOf(expected);
        QVERIFY(index != -1);
        QVERIFY(localDecls[index]->abstractType());
        QCOMPARE(localDecls[index]->abstractType()->toString(), expected_types[offset]);
        offset++;
    }
}

void PyDUChainTest::testVariableCreation_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QStringList>("expected_local_declarations");
    QTest::addColumn<QStringList>("expected_types");

    QTest::newRow("simple") << "a = 3" << QStringList{"a"} << QStringList{"int"};
    QTest::newRow("tuple_wrong") << "a, b = 3" << QStringList{"a", "b"} << QStringList{"mixed", "mixed"};
    QTest::newRow("tuple_unpack_inplace") << "a, b = 3, 5.5" << QStringList{"a", "b"} << QStringList{"int", "float"};
    QTest::newRow("tuple_unpack_indirect") << "c = 3, 3.5\na, b = c" << QStringList{"a", "b"} << QStringList{"int", "float"};
    QTest::newRow("tuple_unpack_stacked_inplace") << "a, (b, c) = 1, 2, 3.5" << QStringList{"a", "b", "c"}
                                                                             << QStringList{"int", "int", "float"};
    QTest::newRow("tuple_unpack_stacked_indirect") << "d = 3.5, 3, 1\na, (b, c) = d"
                                                   << QStringList{"a", "b", "c"} << QStringList{"float", "int", "int"};
    QTest::newRow("unpack_from_list_inplace") << "a, b = [1, 2, 3]" << QStringList{"a", "b"} << QStringList{"int", "int"};
    QTest::newRow("unpack_from_list_indirect") << "c = [1, 2, 3]\na, b = c" << QStringList{"a", "b"}
                                                                            << QStringList{"int", "int"};
    QTest::newRow("for_loop_simple") << "for i in range(3): pass" << QStringList{"i"} << QStringList{"int"};
    QTest::newRow("for_loop_unpack") << "for a, b in [(3, 5.1)]: pass" << QStringList{"a", "b"}
                                                                       << QStringList{"int", "float"};
    QTest::newRow("for_loop_stacked") << "for a, (b, c) in [(1, 2, 3.5)]: pass" << QStringList{"a", "b", "c"}
                                                                                << QStringList{"int", "int", "float"};
}

