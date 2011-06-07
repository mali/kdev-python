#!/usr/bin/env python2.7
# -*- coding: utf-8 -*-
""":synopsis: Base classes for SAX event handlers.
"""
class ContentHandler:


	"""
	This is the main callback interface in SAX, and the one most important to
	applications. The order of events in this interface mirrors the order of the
	information in the document.
	
	
	"""
	
	
	def __init__(self, ):
		pass
	
	def setDocumentLocator(self, locator):
		"""
		Called by the parser to give the application a locator for locating the origin
		of document events.
		
		SAX parsers are strongly encouraged (though not absolutely required) to supply a
		locator: if it does so, it must supply the locator to the application by
		invoking this method before invoking any of the other methods in the
		DocumentHandler interface.
		
		The locator allows the application to determine the end position of any
		document-related event, even if the parser is not reporting an error. Typically,
		the application will use this information for reporting its own errors (such as
		character content that does not match an application's business rules). The
		information returned by the locator is probably not sufficient for use with a
		search engine.
		
		Note that the locator will return correct information only during the invocation
		of the events in this interface. The application should not attempt to use it at
		any other time.
		
		
		"""
		pass
		
	def startDocument(self, ):
		"""
		Receive notification of the beginning of a document.
		
		The SAX parser will invoke this method only once, before any other methods in
		this interface or in DTDHandler (except for :meth:`setDocumentLocator`).
		
		
		"""
		pass
		
	def endDocument(self, ):
		"""
		Receive notification of the end of a document.
		
		The SAX parser will invoke this method only once, and it will be the last method
		invoked during the parse. The parser shall not invoke this method until it has
		either abandoned parsing (because of an unrecoverable error) or reached the end
		of input.
		
		
		"""
		pass
		
	def startPrefixMapping(self, prefix,uri):
		"""
		Begin the scope of a prefix-URI Namespace mapping.
		
		The information from this event is not necessary for normal Namespace
		processing: the SAX XML reader will automatically replace prefixes for element
		and attribute names when the ``feature_namespaces`` feature is enabled (the
		default).
		
		There are cases, however, when applications need to use prefixes in character
		data or in attribute values, where they cannot safely be expanded automatically;
		the :meth:`startPrefixMapping` and :meth:`endPrefixMapping` events supply the
		information to the application to expand prefixes in those contexts itself, if
		necessary.
		
		.. s not really the default, is it? MvL
		
		Note that :meth:`startPrefixMapping` and :meth:`endPrefixMapping` events are not
		guaranteed to be properly nested relative to each-other: all
		:meth:`startPrefixMapping` events will occur before the corresponding
		:meth:`startElement` event, and all :meth:`endPrefixMapping` events will occur
		after the corresponding :meth:`endElement` event, but their order is not
		guaranteed.
		
		
		"""
		pass
		
	def endPrefixMapping(self, prefix):
		"""
		End the scope of a prefix-URI mapping.
		
		See :meth:`startPrefixMapping` for details. This event will always occur after
		the corresponding :meth:`endElement` event, but the order of
		:meth:`endPrefixMapping` events is not otherwise guaranteed.
		
		
		"""
		pass
		
	def startElement(self, name,attrs):
		"""
		Signals the start of an element in non-namespace mode.
		
		The *name* parameter contains the raw XML 1.0 name of the element type as a
		string and the *attrs* parameter holds an object of the :class:`Attributes`
		interface (see :ref:`attributes-objects`) containing the attributes of
		the element.  The object passed as *attrs* may be re-used by the parser; holding
		on to a reference to it is not a reliable way to keep a copy of the attributes.
		To keep a copy of the attributes, use the :meth:`copy` method of the *attrs*
		object.
		
		
		"""
		pass
		
	def endElement(self, name):
		"""
		Signals the end of an element in non-namespace mode.
		
		The *name* parameter contains the name of the element type, just as with the
		:meth:`startElement` event.
		
		
		"""
		pass
		
	def startElementNS(self, name,qname,attrs):
		"""
		Signals the start of an element in namespace mode.
		
		The *name* parameter contains the name of the element type as a ``(uri,
		localname)`` tuple, the *qname* parameter contains the raw XML 1.0 name used in
		the source document, and the *attrs* parameter holds an instance of the
		:class:`AttributesNS` interface (see :ref:`attributes-ns-objects`)
		containing the attributes of the element.  If no namespace is associated with
		the element, the *uri* component of *name* will be ``None``.  The object passed
		as *attrs* may be re-used by the parser; holding on to a reference to it is not
		a reliable way to keep a copy of the attributes.  To keep a copy of the
		attributes, use the :meth:`copy` method of the *attrs* object.
		
		Parsers may set the *qname* parameter to ``None``, unless the
		``feature_namespace_prefixes`` feature is activated.
		
		
		"""
		pass
		
	def endElementNS(self, name,qname):
		"""
		Signals the end of an element in namespace mode.
		
		The *name* parameter contains the name of the element type, just as with the
		:meth:`startElementNS` method, likewise the *qname* parameter.
		
		
		"""
		pass
		
	def characters(self, content):
		"""
		Receive notification of character data.
		
		The Parser will call this method to report each chunk of character data. SAX
		parsers may return all contiguous character data in a single chunk, or they may
		split it into several chunks; however, all of the characters in any single event
		must come from the same external entity so that the Locator provides useful
		information.
		
		*content* may be a Unicode string or a byte string; the ``expat`` reader module
		produces always Unicode strings.
		
		"""
		pass
		
	def ignorableWhitespace(self, whitespace):
		"""
		Receive notification of ignorable whitespace in element content.
		
		Validating Parsers must use this method to report each chunk of ignorable
		whitespace (see the W3C XML 1.0 recommendation, section 2.10): non-validating
		parsers may also use this method if they are capable of parsing and using
		content models.
		
		SAX parsers may return all contiguous whitespace in a single chunk, or they may
		split it into several chunks; however, all of the characters in any single event
		must come from the same external entity, so that the Locator provides useful
		information.
		
		
		"""
		pass
		
	def processingInstruction(self, target,data):
		"""
		Receive notification of a processing instruction.
		
		The Parser will invoke this method once for each processing instruction found:
		note that processing instructions may occur before or after the main document
		element.
		
		A SAX parser should never report an XML declaration (XML 1.0, section 2.8) or a
		text declaration (XML 1.0, section 4.3.1) using this method.
		
		
		"""
		pass
		
	def skippedEntity(self, name):
		"""
		Receive notification of a skipped entity.
		
		The Parser will invoke this method once for each entity skipped. Non-validating
		processors may skip entities if they have not seen the declarations (because,
		for example, the entity was declared in an external DTD subset). All processors
		may skip external entities, depending on the values of the
		``feature_external_ges`` and the ``feature_external_pes`` properties.
		
		
		.. TDHandler Objects
		------------------
		
		:class:`DTDHandler` instances provide the following methods:
		
		
		"""
		pass
		
	


class DTDHandler:


	"""
	Handle DTD events.
	
	This interface specifies only those DTD events required for basic parsing
	(unparsed entities and attributes).
	
	
	"""
	
	
	def __init__(self, ):
		pass
	
	def notationDecl(self, name,publicId,systemId):
		"""
		Handle a notation declaration event.
		
		
		"""
		pass
		
	def unparsedEntityDecl(self, name,publicId,systemId,ndata):
		"""
		Handle an unparsed entity declaration event.
		
		
		.. ntityResolver Objects
		----------------------
		
		
		"""
		pass
		
	


class EntityResolver:


	"""
	Basic interface for resolving entities. If you create an object implementing
	this interface, then register the object with your Parser, the parser will call
	the method in your object to resolve all external entities.
	
	
	"""
	
	
	def __init__(self, ):
		pass
	
	def resolveEntity(self, publicId,systemId):
		"""
		Resolve the system identifier of an entity and return either the system
		identifier to read from as a string, or an InputSource to read from. The default
		implementation returns *systemId*.
		
		
		.. rrorHandler Objects
		--------------------
		
		Objects with this interface are used to receive error and warning information
		from the :class:`XMLReader`.  If you create an object that implements this
		interface, then register the object with your :class:`XMLReader`, the parser
		will call the methods in your object to report all warnings and errors. There
		are three levels of errors available: warnings, (possibly) recoverable errors,
		and unrecoverable errors.  All methods take a :exc:`SAXParseException` as the
		only parameter.  Errors and warnings may be converted to an exception by raising
		the passed-in exception object.
		
		
		"""
		pass
		
	


class ErrorHandler:


	"""
	Interface used by the parser to present error and warning messages to the
	application.  The methods of this object control whether errors are immediately
	converted to exceptions or are handled in some other way.
	
	In addition to these classes, :mod:`xml.sax.handler` provides symbolic constants
	for the feature and property names.
	
	
	"""
	
	
	def __init__(self, ):
		pass
	
	def error(self, exception):
		"""
		Called when the parser encounters a recoverable error.  If this method does not
		raise an exception, parsing may continue, but further document information
		should not be expected by the application.  Allowing the parser to continue may
		allow additional errors to be discovered in the input document.
		
		
		"""
		pass
		
	def fatalError(self, exception):
		"""
		Called when the parser encounters an error it cannot recover from; parsing is
		expected to terminate when this method returns.
		
		
		"""
		pass
		
	"""
	| value: ``"http://xml.org/sax/features/namespaces"``
	| true: Perform Namespace processing.
	| false: Optionally do not perform Namespace processing (implies
	namespace-prefixes; default).
	| access: (parsing) read-only; (not parsing) read/write
	
	
	"""
	feature_namespaces = None
	"""
	| value: ``"http://xml.org/sax/features/namespace-prefixes"``
	| true: Report the original prefixed names and attributes used for Namespace
	declarations.
	| false: Do not report attributes used for Namespace declarations, and
	optionally do not report original prefixed names (default).
	| access: (parsing) read-only; (not parsing) read/write
	
	
	"""
	feature_namespace_prefixes = None
	"""
	| value: ``"http://xml.org/sax/features/string-interning"``
	| true: All element names, prefixes, attribute names, Namespace URIs, and
	local names are interned using the built-in intern function.
	| false: Names are not necessarily interned, although they may be (default).
	| access: (parsing) read-only; (not parsing) read/write
	
	
	"""
	feature_string_interning = None
	"""
	| value: ``"http://xml.org/sax/features/validation"``
	| true: Report all validation errors (implies external-general-entities and
	external-parameter-entities).
	| false: Do not report validation errors.
	| access: (parsing) read-only; (not parsing) read/write
	
	
	"""
	feature_validation = None
	"""
	| value: ``"http://xml.org/sax/features/external-general-entities"``
	| true: Include all external general (text) entities.
	| false: Do not include external general entities.
	| access: (parsing) read-only; (not parsing) read/write
	
	
	"""
	feature_external_ges = None
	"""
	| value: ``"http://xml.org/sax/features/external-parameter-entities"``
	| true: Include all external parameter entities, including the external DTD
	subset.
	| false: Do not include any external parameter entities, even the external
	DTD subset.
	| access: (parsing) read-only; (not parsing) read/write
	
	
	"""
	feature_external_pes = None
	"""
	List of all features.
	
	
	"""
	all_features = None
	"""
	| value: ``"http://xml.org/sax/properties/lexical-handler"``
	| data type: xml.sax.sax2lib.LexicalHandler (not supported in Python 2)
	| description: An optional extension handler for lexical events like
	comments.
	| access: read/write
	
	
	"""
	property_lexical_handler = None
	"""
	| value: ``"http://xml.org/sax/properties/declaration-handler"``
	| data type: xml.sax.sax2lib.DeclHandler (not supported in Python 2)
	| description: An optional extension handler for DTD-related events other
	than notations and unparsed entities.
	| access: read/write
	
	
	"""
	property_declaration_handler = None
	"""
	| value: ``"http://xml.org/sax/properties/dom-node"``
	| data type: org.w3c.dom.Node (not supported in Python 2)
	| description: When parsing, the current DOM node being visited if this is
	a DOM iterator; when not parsing, the root DOM node for iteration.
	| access: (parsing) read-only; (not parsing) read/write
	
	
	"""
	property_dom_node = None
	"""
	| value: ``"http://xml.org/sax/properties/xml-string"``
	| data type: String
	| description: The literal string of characters that was the source for the
	current event.
	| access: read-only
	
	
	"""
	property_xml_string = None
	"""
	List of all known property names.
	
	
	.. ontentHandler Objects
	----------------------
	
	Users are expected to subclass :class:`ContentHandler` to support their
	application.  The following methods are called by the parser on the appropriate
	events in the input document:
	
	
	"""
	all_properties = None
	

