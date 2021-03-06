/*****************************************************************************
 * This file is part of KDevelop                                             *
 * Copyright 2011-2013 Sven Brauch <svenbrauch@googlemail.com>               *
 *                                                                           *
 * This program is free software; you can redistribute it and/or             *
 * modify it under the terms of the GNU General Public License as            *
 * published by the Free Software Foundation; either version 2 of            *
 * the License, or (at your option) any later version.                       *
 *                                                                           *
 * This program is distributed in the hope that it will be useful,           *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU General Public License for more details.                              *
 *                                                                           *
 * You should have received a copy of the GNU General Public License         *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.     *
 *****************************************************************************
 */

#include "helpers.h"

#include <QList>
#include <KUrl>
#include <KDebug>
#include <KStandardDirs>
#include <QProcess>

#include <language/duchain/types/unsuretype.h>
#include <language/duchain/types/integraltype.h>
#include <language/duchain/types/containertypes.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchain.h>
#include <language/duchain/classdeclaration.h>
#include <language/duchain/aliasdeclaration.h>
#include <language/backgroundparser/backgroundparser.h>
#include <interfaces/iproject.h>
#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/idocumentcontroller.h>

#include <KTextEditor/View>

#include "ast.h"
#include "types/hintedtype.h"
#include "types/unsuretype.h"
#include "types/indexedcontainer.h"
#include "kdevpythonversion.h"
#include <language/duchain/types/typeutils.h>

using namespace KDevelop;

namespace Python {

QList<KUrl> Helper::cachedSearchPaths;
QStringList Helper::dataDirs;
QString Helper::documentationFile;
DUChainPointer<TopDUContext> Helper::documentationFileContext = DUChainPointer<TopDUContext>(0);
QStringList Helper::correctionFileDirs;
QString Helper::localCorrectionFileDir;

void Helper::scheduleDependency(const IndexedString& dependency, int betterThanPriority)
{
    BackgroundParser* bgparser = KDevelop::ICore::self()->languageController()->backgroundParser();
    bool needsReschedule = true;
    if ( bgparser->isQueued(dependency) ) {
        const ParseJob* job = bgparser->parseJobForDocument(dependency);
        int previousPriority = BackgroundParser::WorstPriority;
        if ( job ) {
            previousPriority = job->parsePriority();
        }
        // if it's less important, reschedule it
        if ( job && previousPriority > betterThanPriority - 1 ) {
            bgparser->removeDocument(dependency);
        }
        else if ( job ) {
            needsReschedule = false;
        }
    }
    if ( needsReschedule ) {
        bgparser->addDocument(dependency, TopDUContext::ForceUpdate, betterThanPriority - 1,
                              0, ParseJob::FullSequentialProcessing);
    }
}

IndexedDeclaration Helper::declarationUnderCursor(bool allowUse)
{
    KDevelop::IDocument* doc = ICore::self()->documentController()->activeDocument();
    if ( doc && doc->textDocument() && doc->textDocument()->activeView() ) {
        DUChainReadLocker lock;
        if ( allowUse ) {
            return DUChainUtils::itemUnderCursor(doc->url(), SimpleCursor(doc->textDocument()->activeView()->cursorPosition()));
        }
        else {
            return DUChainUtils::declarationInLine(SimpleCursor(doc->textDocument()->activeView()->cursorPosition()), DUChainUtils::standardContextForUrl(doc->url()));
        }
    }

    return KDevelop::IndexedDeclaration();
}

Declaration* Helper::accessAttribute(Declaration* accessed, const QString& attribute, const DUContext* current)
{
    if ( ! accessed || ! accessed->abstractType() ) {
        return 0;
    }
    // if the type is unsure, search all the possibilities
    auto structureTypes = Helper::filterType<StructureType>(accessed->abstractType(),
        [](AbstractType::Ptr toFilter) {
            auto type = Helper::resolveAliasType(toFilter);
            return type && type->whichType() == AbstractType::TypeStructure;
        }
    );
    for ( auto type: structureTypes ) {
        QList<DUContext*> searchContexts = Helper::internalContextsForClass(type, current->topContext());
        for ( DUContext* c: searchContexts ) {
            QList< Declaration* > found = c->findDeclarations(KDevelop::Identifier(attribute),
                                                              CursorInRevision::invalid(),
                                                              current->topContext(), DUContext::DontSearchInParent);
            if ( ! found.isEmpty() ) {
                return found.first();
            }
        }
    }
    return nullptr;
}

AbstractType::Ptr Helper::resolveAliasType(const AbstractType::Ptr eventualAlias)
{
    return TypeUtils::resolveAliasType(eventualAlias);
}

AbstractType::Ptr Helper::extractTypeHints(AbstractType::Ptr type, TopDUContext* current)
{
    UnsureType::Ptr result(new UnsureType());
    unsigned short maxHints = 7;
    if ( HintedType::Ptr hinted = type.cast<HintedType>() ) {
        if ( hinted->isValid(current) && isUsefulType(hinted.cast<AbstractType>()) ) {
            result->addType(type->indexed());
        }
    }
    else if ( UnsureType::Ptr unsure = type.cast<UnsureType>() ) {
        int len = unsure->typesSize();
        for ( int i = 0; i < len and i < maxHints; i++ ) {
            if ( HintedType::Ptr hinted = unsure->types()[i].abstractType().cast<HintedType>() ) {
                if ( hinted->isValid(current) ) {
                    kDebug() << "Adding type hint (multi): " << hinted->toString();
                    result->addType(hinted->indexed());
                }
                else {
                    kDebug() << "Discarding type hint (multi): " << hinted->toString();
                    maxHints += 1;
                }
            }
            else {
                maxHints += 1;
            }
        }
    }
    else if ( IndexedContainer::Ptr indexed = type.cast<IndexedContainer>() ) {
        // TODO this is bad because it is slow. Make it faster!
        // TODO this would really be an important thing, as it will be called quite often.
        // TODO "how" is simple, just avoid cloning stuff if nothing needs to be changed (i.e. no hints exist).
        IndexedContainer::Ptr edit = IndexedContainer::Ptr(static_cast<IndexedContainer*>(indexed->clone()));
        for ( int i = 0; i < indexed->typesCount(); i++ ) {
            if ( HintedType::Ptr p = indexed->typeAt(i).abstractType().cast<HintedType>() ) {
                if ( ! p->isValid(current) ) {
                    edit->replaceType(i, AbstractType::Ptr(new IntegralType(IntegralType::TypeMixed)));
                }
            }
        }
        return edit.cast<AbstractType>();
    }
    else if ( auto variable = type.cast<ListType>() ) {
        auto edit = ListType::Ptr(static_cast<ListType*>(variable->clone()));
        UnsureType::Ptr newContentType(new UnsureType());
        AbstractType::Ptr oldContentType = edit->contentType().abstractType();
        bool isHint = false;
        if ( oldContentType ) {
            if ( UnsureType::Ptr oldUnsure = oldContentType.cast<UnsureType>() ) {
                for ( unsigned int i = 0; i < oldUnsure->typesSize(); i++ ) {
                    if ( HintedType::Ptr hinted = oldUnsure->types()[i].abstractType().cast<HintedType>() ) {
                        isHint = true;
                        if ( ! hinted->isValid(current) ) {
                            continue;
                        }
                    }
                    newContentType->addType(oldUnsure->types()[i]);
                }
            }
            else if ( HintedType::Ptr hinted = oldContentType.cast<HintedType>() ) {
                isHint = true;
                if ( hinted->isValid(current) ) {
                    newContentType->addType(hinted->indexed());
                }
            }
            if ( ! isHint ) {
                return result.cast<AbstractType>();
            }
            edit->replaceContentType(newContentType.cast<AbstractType>());
//             edit->replaceKeyType(variable->keyType().abstractType()); // TODO re-enable?
        }
        return edit.cast<AbstractType>();
    }
    return result.cast<AbstractType>();
}

Helper::FuncInfo Helper::functionDeclarationForCalledDeclaration(DeclarationPointer ptr)
{
    if ( ! ptr ) {
        return FuncInfo();
    }
    bool isConstructor = false;
    DeclarationPointer calledDeclaration = ptr;
    if ( ! calledDeclaration->isFunctionDeclaration() ) {
        // not a function -- try looking for a constructor
        StructureType::Ptr classType = calledDeclaration->type<StructureType>();
        auto contexts = Helper::internalContextsForClass(classType, ptr->topContext());
        for ( DUContext* context: contexts ) {
            static KDevelop::Identifier initIdentifier("__init__");
            QList<Declaration*> constructors = context->findDeclarations(initIdentifier);
            if ( ! constructors.isEmpty() ) {
                calledDeclaration = dynamic_cast<FunctionDeclaration*>(constructors.first());
                isConstructor = true;
                break;
            }
        }
    }
    FunctionDeclarationPointer lastFunctionDeclaration;
    if ( calledDeclaration ) {
        // It was a class -- use the constructor
        lastFunctionDeclaration = calledDeclaration.dynamicCast<FunctionDeclaration>();
    }
    else {
        // Use the original declaration
        lastFunctionDeclaration = ptr.dynamicCast<FunctionDeclaration>();
    }
    return QPair<FunctionDeclarationPointer, bool>(lastFunctionDeclaration, isConstructor);
}

Declaration* Helper::declarationForName(const QualifiedIdentifier& identifier, const RangeInRevision& nodeRange,
                                        KDevelop::DUChainPointer<const DUContext> context)
{
    QList<Declaration*> declarations;
    QList<Declaration*> localDeclarations;
    QList<Declaration*> importedLocalDeclarations;
    {
        DUChainReadLocker lock(DUChain::lock());
        if ( context.data() == context->topContext() and nodeRange.isValid() ) {
            declarations = context->topContext()->findDeclarations(identifier, nodeRange.end);
        }
        else {
            declarations = context->topContext()->findDeclarations(identifier, CursorInRevision::invalid());
        }
        localDeclarations = context->findLocalDeclarations(identifier.last(), nodeRange.end, 0,
                                                           AbstractType::Ptr(0), DUContext::DontResolveAliases);
        importedLocalDeclarations = context->findDeclarations(identifier.last(), nodeRange.end);
    }
    Declaration* declaration = 0;
    if ( localDeclarations.length() ) {
        declaration = localDeclarations.last();
    }
    else if ( importedLocalDeclarations.length() ) {
        // don't use declarations from class decls, they must be referenced through "self.<foo>"
        do {
            declaration = importedLocalDeclarations.last();
            importedLocalDeclarations.pop_back();
            if ( not declaration or declaration->context()->type() == DUContext::Class ) {
                declaration = 0;
            }
            if ( importedLocalDeclarations.isEmpty() ) {
                break;
            }
        } while ( ! importedLocalDeclarations.isEmpty() );
    }

    if ( !declaration && declarations.length() ) {
        declaration = declarations.last();
    }
    return declaration;
}

QList< DUContext* > Helper::internalContextsForClass(StructureType::Ptr klassType, TopDUContext* context, ContextSearchFlags flags, int depth)
{
    QList<DUContext*> searchContexts;
    if ( ! klassType ) {
        return searchContexts;
    }
    if ( auto c = klassType->internalContext(context) ) {
        searchContexts << c;
    }
    Declaration* decl = Helper::resolveAliasDeclaration(klassType->declaration(context));
    ClassDeclaration* klass = dynamic_cast<ClassDeclaration*>(decl);
    if ( klass ) {
        FOREACH_FUNCTION ( const BaseClassInstance& base, klass->baseClasses ) {
            if ( flags == PublicOnly and base.access == KDevelop::Declaration::Private ) {
                continue;
            }
            StructureType::Ptr baseClassType = base.baseClass.type<StructureType>();
            // recursive call, because the base class will have more base classes eventually
            if ( depth < 10 ) {
                searchContexts.append(Helper::internalContextsForClass(baseClassType, context, flags, depth + 1));
            }
        }
    }
    return searchContexts;
}

Declaration* Helper::resolveAliasDeclaration(Declaration* decl)
{
    AliasDeclaration* alias = dynamic_cast<AliasDeclaration*>(decl);
    if ( alias ) {
        DUChainReadLocker lock;
        return alias->aliasedDeclaration().data();
    }
    else
        return decl;
}

QStringList Helper::getDataDirs() {
    if ( Helper::dataDirs.isEmpty() ) {
        KStandardDirs d;
        Helper::dataDirs = d.findDirs("data", "kdevpythonsupport/documentation_files");
    }
    return Helper::dataDirs;
}

QString Helper::getDocumentationFile() {
    if ( Helper::documentationFile.isNull() ) {
        Helper::documentationFile = KStandardDirs::locate("data", "kdevpythonsupport/documentation_files/builtindocumentation.py");
    }
    return Helper::documentationFile;
}

ReferencedTopDUContext Helper::getDocumentationFileContext()
{
    if ( Helper::documentationFileContext ) {
        return ReferencedTopDUContext(Helper::documentationFileContext.data());
    }
    else {
        DUChainReadLocker lock;
        ReferencedTopDUContext ctx = ReferencedTopDUContext(DUChain::self()->chainForDocument(Helper::getDocumentationFile()));
        Helper::documentationFileContext = DUChainPointer<TopDUContext>(ctx.data());
        return ctx;
    }
    return ReferencedTopDUContext(0); // c++...
}

KUrl Helper::getCorrectionFile(KUrl document)
{
    if ( Helper::correctionFileDirs.isEmpty() ) {
        KStandardDirs d;
        Helper::correctionFileDirs = d.findDirs("data", "kdevpythonsupport/correction_files/");
    }

    foreach (QString correctionFileDir, correctionFileDirs) {
        foreach ( const KUrl& basePath, Helper::getSearchPaths(KUrl()) ) {
            if ( ! basePath.isParentOf(document) ) {
                continue;
            }
            QString path = KUrl::relativePath(basePath.path(), document.path());
            KUrl absolutePath(correctionFileDir + path);
            absolutePath.cleanPath();

            if ( QFile::exists(absolutePath.path()) ) {
                return absolutePath;
            }
        }
    }
    return KUrl();
}

KUrl Helper::getLocalCorrectionFile(KUrl document)
{
    if ( Helper::localCorrectionFileDir.isNull() ) {
        Helper::localCorrectionFileDir = KStandardDirs::locateLocal("data", "kdevpythonsupport/correction_files/");
    }

    KUrl absolutePath;
    foreach ( const KUrl& basePath, Helper::getSearchPaths(KUrl()) ) {
        if ( ! basePath.isParentOf(document) ) {
            continue;
        }
        QString path = KUrl::relativePath(basePath.path(), document.path());
        absolutePath = KUrl(Helper::localCorrectionFileDir + path);
        absolutePath.cleanPath();

        break;
    }
    return absolutePath;
}
    
QList<KUrl> Helper::getSearchPaths(KUrl workingOnDocument)
{
    QList<KUrl> searchPaths;
    // search in the projects, as they're packages and likely to be installed or added to PYTHONPATH later
    foreach  (IProject* project, ICore::self()->projectController()->projects() ) {
        searchPaths.append(KUrl(project->folder().url()));
    }
    
    foreach ( const QString& path, getDataDirs() ) {
        searchPaths.append(KUrl(path));
    }
    
    if ( cachedSearchPaths.isEmpty() ) {
        KStandardDirs d;
        kDebug() << "*** Gathering search paths...";
        QStringList getpath;
        getpath << "-c" << "import sys; sys.stdout.write(':'.join(sys.path))";
        
        QProcess python;
        python.start(QLatin1String(PYTHON_EXECUTABLE), getpath);
        python.waitForFinished(1000);
        QByteArray pythonpath = python.readAllStandardOutput();
        QList<QByteArray> paths = pythonpath.split(':');
        paths.removeAll("");
        
        if ( ! pythonpath.isEmpty() ) {
            foreach ( const QString& path, paths ) {
                cachedSearchPaths.append(path);
            }
        }
        else {
            kWarning() << "Could not get search paths! Defaulting to stupid stuff.";
            searchPaths.append(KUrl("/usr/lib/python2.7"));
            searchPaths.append(KUrl("/usr/lib/python2.7/site-packages"));
            QString path = qgetenv("PYTHONPATH");
            QStringList paths = path.split(':');
            foreach ( const QString& path, paths ) {
                cachedSearchPaths.append(path);
            }
        }
        kDebug() << " *** Done. Got search paths: " << cachedSearchPaths;
    }
    else {
        kDebug() << " --- Search paths from cache: " << cachedSearchPaths;
    }
    
    searchPaths.append(cachedSearchPaths);
    
    const QString& currentDir = workingOnDocument.directory(KUrl::IgnoreTrailingSlash);
    if ( ! currentDir.isEmpty() ) {
        // search in the current packages
        searchPaths.append(KUrl(currentDir));
    }
    
    return searchPaths;
}

bool Helper::isUsefulType(AbstractType::Ptr type)
{
    return TypeUtils::isUsefulType(type);
}

AbstractType::Ptr Helper::contentOfIterable(const AbstractType::Ptr iterable)
{
    auto items = filterType<AbstractType>(iterable,
        [](AbstractType::Ptr t) {
            return ListType::Ptr::dynamicCast(t) || IndexedContainer::Ptr::dynamicCast(t);
        },
        [](AbstractType::Ptr t) {
            if ( auto variable = ListType::Ptr::dynamicCast(t) ) {
                return AbstractType::Ptr(variable->contentType().abstractType());
            }
            else {
                auto indexed = t.cast<IndexedContainer>();
                return indexed->asUnsureType();
            }
        }
    );

    if ( items.size() == 1 ) {
        return items.first();
    }
    auto unsure = AbstractType::Ptr(new UnsureType);
    for ( auto type: items ) {
        Helper::mergeTypes(unsure, type);
    }
    return unsure;
}

AbstractType::Ptr Helper::mergeTypes(AbstractType::Ptr type, const AbstractType::Ptr newType)
{
    UnsureType::Ptr ret;
    ret.count();
    return TypeUtils::mergeTypes<Python::UnsureType>(type, newType);
}

}
