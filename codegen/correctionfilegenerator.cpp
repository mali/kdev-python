/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2013 Sven Brauch <svenbrauch@googlemail.com>                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "correctionfilegenerator.h"

#include <QTemporaryFile>
#include <QAbstractFileEngine>

#include <language/backgroundparser/backgroundparser.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainutils.h>
#include <language/interfaces/codecontext.h>
#include <interfaces/icore.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/iprojectcontroller.h>
#include <parser/parsesession.h>

#include <KTextEditor/Document>

#include "duchain/helpers.h"
#include "parser/codehelpers.h"

using namespace KDevelop;

namespace Python {

TypeCorrection::TypeCorrection()
    : m_ui(new Ui::CorrectionWidget)
{
}

TypeCorrection &TypeCorrection::self()
{
    static TypeCorrection instance;
    return instance;
}

void TypeCorrection::doContextMenu(ContextMenuExtension &extension, Context *context)
{
    if ( DeclarationContext* declContext = dynamic_cast<DeclarationContext*>(context) ) {
        qRegisterMetaType<KDevelop::IndexedDeclaration>("KDevelop::IndexedDeclaration");

        DUChainReadLocker lock;

        KDevelop::Declaration* declaration = declContext->declaration().data();

        if ( declaration && (declaration->kind() == Declaration::Instance
                             || (declaration->kind() == Declaration::Type
                                 && declaration->abstractType()->whichType() == AbstractType::TypeFunction)) ) {
            QAction* action = new QAction(i18n("Specify type for \"%1\"...", declaration->qualifiedIdentifier().toString()), 0);
            action->setData(QVariant::fromValue(IndexedDeclaration(declaration)));
            action->setIcon(KIcon("code-class"));
            connect(action, SIGNAL(triggered(bool)), this, SLOT(executeSpecifyTypeAction()));

            extension.addAction(ContextMenuExtension::ExtensionGroup, action);
        }
    }
}

void TypeCorrection::executeSpecifyTypeAction()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if ( ! action ) {
        kWarning() << "slot not invoked by triggering a QAction, should not happen"; // :)
        return;
    }

    DUChainReadLocker lock;
    IndexedDeclaration decl = action->data().value<IndexedDeclaration>();
    if ( ! decl.isValid() ) {
        decl = Helper::declarationUnderCursor();
    }

    if ( ! decl.isValid() ) {
        kWarning() << "No declaration found!";
        return;
    }

    CorrectionFileGenerator::HintType hintType;
    if ( decl.data()->isFunctionDeclaration() ) {
        hintType = CorrectionFileGenerator::FunctionReturnHint;
    }
    else if ( decl.data()->kind() == Declaration::Instance ) {
        hintType = CorrectionFileGenerator::LocalVariableHint;
    }
    else {
        kWarning() << "Correction requested for something that's not a local variable or function.";
        return;
    }

    CorrectionAssistant *dialog = new CorrectionAssistant(decl, hintType);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCaption("Specify type for " + decl.data()->identifier().toString());
    dialog->setButtons(KDialog::Ok | KDialog::Cancel);
    connect(dialog, SIGNAL(accepted()), this, SLOT(accepted()));

    m_ui->setupUi(dialog->mainWidget());
    if ( hintType == CorrectionFileGenerator::FunctionReturnHint ) {
        m_ui->kindLabel->setText(i18n("Function return type"));
    }
    else if ( hintType == CorrectionFileGenerator::LocalVariableHint ) {
        m_ui->kindLabel->setText(i18n("Local variable"));
    }

    m_ui->identifierLabel->setText(decl.data()->qualifiedIdentifier().toString());
    m_ui->typeText->setFocus();
    dialog->resize(560, 180);
    lock.unlock();

    dialog->show();
}

void TypeCorrection::accepted()
{
    CorrectionAssistant *dialog = qobject_cast<CorrectionAssistant*>(sender());
    Q_ASSERT(dialog);
    if ( ! dialog ) {
        kWarning() << "accepted() called without a sender";
        return;
    }

    DUChainReadLocker lock;
    IndexedDeclaration decl;

    decl = dialog->declaration();

    if ( ! decl.isValid() ) {
        decl = Helper::declarationUnderCursor();
    }

    if ( ! decl.isValid() ) {
        kWarning() << "No declaration found!";
        return;
    }

    KUrl correctionFile = Helper::getLocalCorrectionFile(decl.data()->topContext()->url().toUrl());
    if ( correctionFile.isEmpty() ) {
        KMessageBox::error(0, i18n("Sorry, cannot create hints for files which are not part of a project."));
        return;
    }
    CorrectionFileGenerator generator(correctionFile.path());

    CorrectionFileGenerator::HintType hintType = dialog->hintType();

    generator.addHint(m_ui->typeText->text(), m_ui->importsText->text().split(',', QString::SkipEmptyParts), decl.data(), hintType);

    kDebug() << "Forcing a reparse on " << decl.data()->topContext()->url();
    ICore::self()->languageController()->backgroundParser()->addDocument(IndexedString(decl.data()->topContext()->url()),
                                                                         TopDUContext::ForceUpdate);
    ICore::self()->languageController()->backgroundParser()->addDocument(IndexedString(correctionFile),
                                                                         TopDUContext::ForceUpdate);
}

CorrectionFileGenerator::CorrectionFileGenerator(const QString &filePath)
    : m_file(filePath)
{
    Q_ASSERT(! filePath.isEmpty());
    kDebug() << "Correction file path: " << filePath;

    QFileInfo info(m_file);
    if ( ! info.absoluteDir().exists() ) {
        kDebug() << "Directory does not exist. Creating...";
        info.absoluteDir().mkpath(info.absolutePath());
    }

    m_file.open(QFile::ReadWrite);

    m_code = QString(m_file.readAll()).split('\n');
    m_oldContents = m_code;
    m_fileIndents.reset(new FileIndentInformation(m_code));
}

void CorrectionFileGenerator::addHint(const QString &typeCode, const QStringList &modules, Declaration *forDeclaration,
                                      CorrectionFileGenerator::HintType hintType)
{
    if ( ! forDeclaration || ! forDeclaration->context() ) {
        kWarning() << "Declaration does not have context!" << (forDeclaration ? forDeclaration->toString() : "");
        return;
    }

    DUContext* context = forDeclaration->context();
    if ( context->type() == DUContext::Function ) {
        auto otherImporters = context->importers();
        if ( otherImporters.isEmpty() ) {
            return;
        }
        context = otherImporters.first();
    }

    // We're in a class if the context of the declaration is a Class or if its
    // parent context is a class. This is because a function body has a context
    // of type Other.
    bool inClass = context->type() == DUContext::Class
            || (context->parentContext() && context->parentContext()->type() == DUContext::Class);

    // If the declaration is part of the function's arguments or it's parent
    // context is one of a function.
    bool inFunction = context->type() == DUContext::Function
            || (context->owner() && context->owner()->abstractType()->whichType() == AbstractType::TypeFunction);

    kDebug() << "Are we in a class: " << inClass;
    kDebug() << "Are we in a function: " << inFunction;

    QString enclosingClassIdentifier, enclosingFunctionIdentifier;

    if ( context->owner() ) {
        if ( inClass && inFunction ) {
            Declaration *functionDeclaration = context->owner();

            enclosingClassIdentifier = functionDeclaration->context()->owner()->identifier().identifier().str();
            enclosingFunctionIdentifier = functionDeclaration->identifier().identifier().str();
        }
        else if ( inClass ) {
            enclosingClassIdentifier = context->owner()->identifier().identifier().str();
        }
        else if ( inFunction ) {
            enclosingFunctionIdentifier = context->owner()->identifier().identifier().str();
        }
    }

    kDebug() << "Enclosing class: " << enclosingClassIdentifier;
    kDebug() << "Enclosing function: " << enclosingFunctionIdentifier;

    QString declarationIdentifier = forDeclaration->identifier().identifier().str();

    bool foundClassDeclaration = false;
    bool foundFunctionDeclaration = false;

    QString functionIdentifier;

    if ( hintType == FunctionReturnHint ) {
        functionIdentifier = declarationIdentifier;
    }
    else if ( hintType == LocalVariableHint ) {
        functionIdentifier = enclosingFunctionIdentifier;
    }

    int line = findStructureFor(enclosingClassIdentifier, functionIdentifier);

    if ( line == -1 ) {
        line = findStructureFor(enclosingClassIdentifier, QString());
    }
    else if ( inFunction || hintType == FunctionReturnHint ) {
        foundFunctionDeclaration = true;
    }

    if ( line == -1 ) {
        line = findStructureFor(QString(), QString());
    }
    else if ( inClass ) {
        foundClassDeclaration = true;
    }

    kDebug() << "Found class declaration: " << foundClassDeclaration << enclosingClassIdentifier;
    kDebug() << "Found function declaration: " << foundFunctionDeclaration << functionIdentifier;
    kDebug() << "Line: " << line;

    int indentsForNextStatement = m_fileIndents->indentForLine(line);

    if ( foundClassDeclaration ) {
        indentsForNextStatement += DEFAULT_INDENT_LEVEL;
    }

    QStringList newCode;
    if ( inClass ) {
        if ( ! foundClassDeclaration ) {
            QString classDeclaration = createStructurePart(enclosingClassIdentifier, ClassType);
            classDeclaration.prepend(QString(indentsForNextStatement, ' '));

            newCode.append(classDeclaration);
            indentsForNextStatement += DEFAULT_INDENT_LEVEL;
        }
        else {
            line++;
        }
    }

    if ( inFunction || hintType == FunctionReturnHint ) {
        if ( ! foundFunctionDeclaration ) {
            QString functionDeclaration;
            if ( inClass ) {
                functionDeclaration = createStructurePart(functionIdentifier, MemberFunctionType);
            }
            else {
                functionDeclaration = createStructurePart(functionIdentifier, FunctionType);
            }
            functionDeclaration.prepend(QString(indentsForNextStatement, ' '));

            newCode.append(functionDeclaration);
            indentsForNextStatement += DEFAULT_INDENT_LEVEL;
        }
        else {
            line++;
        }
    }

    if ( foundFunctionDeclaration && ! foundClassDeclaration ) {
        indentsForNextStatement += DEFAULT_INDENT_LEVEL;
    }
    QString hintCode;
    if ( hintType == FunctionReturnHint ) {
        hintCode = "returns = " + typeCode;
    }
    else if ( hintType == LocalVariableHint ) {
        hintCode = "l_" + declarationIdentifier + " = " + typeCode;
    }
    kDebug() << "Hint code: " << hintCode;
    hintCode.prepend(QString(indentsForNextStatement, ' '));
    newCode.append(hintCode);

    for ( int i = 0; i < newCode.length(); i++ ) {
        m_code.insert(line + i, newCode.at(i));
    }

    // We safely insert any import declaration at the top
    foreach ( const QString &moduleName, modules ) {
        bool importExists = false;
        foreach (const QString &line, m_code) {
            if ( ! line.startsWith("import") && ! line.startsWith("from") && ! line.isEmpty() ) {
                break;
            }

            // In both import ... and from ... import ..., the second part is what we want
            if ( line.section(' ', 1, 1, QString::SectionSkipEmpty) == moduleName.trimmed() ) {
                importExists = true;
            }
        }

        if ( ! importExists ) {
            m_code.prepend("import " + moduleName.trimmed());
        }
    }

    QTemporaryFile temp;
    if ( checkForValidSyntax() && temp.open() ) {
        kDebug() << "File path: " << m_file.fileName();
        kDebug() << "Temporary file path: " << temp.fileName();
        QTextStream stream(&temp);
        stream << m_code.join("\n");
        m_fileIndents.reset(new FileIndentInformation(m_code));
        stream.flush();

        bool success = m_file.remove();
        success = success ? QFile::rename(temp.fileName(), m_file.fileName()) : false;

        if ( success && m_file.open(QFile::ReadWrite) ) {
            kDebug() << "Successfully saved correction file.";

            m_oldContents = m_code;
        }
        else {
            m_code = m_oldContents;
        }
    }
    else {
        kDebug() << "Something went wrong, reverting changes to correction file";
        m_code = m_oldContents;
    }
}

class StructureFindVisitor : public Python::AstDefaultVisitor
{
public:
    StructureFindVisitor(const QString &klass, const QString &function)
        : m_line(-1)
    {
        if ( ! klass.isNull() ) {
            m_searchedDeclaration.push(klass);
        }

        if ( ! function.isNull() ) {
            m_searchedDeclaration.push(function);
        }
    }

    virtual void visitFunctionDefinition(FunctionDefinitionAst* node)
    {
        m_declaration.push(node->name->value);

        if ( m_declaration == m_searchedDeclaration ) {
            m_line = node->startLine;
        }

        AstDefaultVisitor::visitFunctionDefinition(node);

        m_declaration.pop();
    }

    virtual void visitClassDefinition(ClassDefinitionAst* node)
    {
        m_declaration.push(node->name->value);

        if ( m_declaration == m_searchedDeclaration ) {
            m_line = node->startLine;
        }

        AstDefaultVisitor::visitClassDefinition(node);

        m_declaration.pop();
    }

    int line() const
    {
        return m_line;
    }

private:
    QStack<QString> m_searchedDeclaration;
    QStack<QString> m_declaration;
    int m_line;
};

int CorrectionFileGenerator::findStructureFor(const QString &klass, const QString &function)
{
    // If we're not looking for a specific thing, return the end of the file
    if ( klass.isNull() && function.isNull() ) {
        return m_code.length() - 1;
    }

    ParseSession parseSession;
    parseSession.setContents(m_code.join("\n"));

    QPair<CodeAst::Ptr, bool> parsed = parseSession.parse();

    QString classIdentifier = ( ! klass.isNull() ) ? "class_" + klass : QString();
    QString functionIdentifier = ( ! function.isNull() ) ? "function_" + function : QString();

    StructureFindVisitor visitor(classIdentifier, functionIdentifier);
    visitor.visitCode(parsed.first.data());

    return visitor.line();
}

QString CorrectionFileGenerator::createStructurePart(const QString &identifierSuffix, CorrectionFileGenerator::StructureType type)
{
    QString code;
    QString params;

    switch ( type ) {
    case ClassType:
        code = "class class_" + identifierSuffix + ":";
        break;
    case MemberFunctionType:
        params = "self";
    case FunctionType:
        code = "def function_" + identifierSuffix + "(" + params + "):";
        break;
    }

    return code;
}

bool CorrectionFileGenerator::checkForValidSyntax()
{
    ParseSession parseSession;
    parseSession.setContents(m_code.join("\n"));

    QPair<CodeAst::Ptr, bool> parsed = parseSession.parse();

    return parsed.second && parseSession.m_problems.isEmpty();
}

CorrectionAssistant::CorrectionAssistant(IndexedDeclaration declaration, CorrectionFileGenerator::HintType hintType,
                                         QWidget *parent)
    : KDialog(parent),
      m_declaration(declaration),
      m_hintType(hintType)
{

}

IndexedDeclaration CorrectionAssistant::declaration() const
{
    return m_declaration;
}

CorrectionFileGenerator::HintType CorrectionAssistant::hintType() const
{
    return m_hintType;
}

}
