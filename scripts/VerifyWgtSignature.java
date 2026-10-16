/* Verify WGT XML signatures with the JDK's XMLDSig implementation.
 * Verifies integrity and key possession, not Samsung device certificate trust. */
import java.io.*;
import java.net.URI;
import java.security.Key;
import java.security.cert.X509Certificate;
import java.security.interfaces.RSAPublicKey;
import java.util.*;
import java.util.zip.*;
import javax.xml.XMLConstants;
import javax.xml.crypto.*;
import javax.xml.crypto.dsig.*;
import javax.xml.crypto.dsig.dom.DOMValidateContext;
import javax.xml.crypto.dsig.keyinfo.*;
import javax.xml.parsers.*;
import org.w3c.dom.*;
public final class VerifyWgtSignature {
    static final class Keys extends KeySelector {
        public KeySelectorResult select(KeyInfo info,Purpose purpose,AlgorithmMethod method,XMLCryptoContext context) throws KeySelectorException {
            if(info==null)throw new KeySelectorException("Missing KeyInfo");
            for(Object object:info.getContent())if(object instanceof X509Data) {
                for(Object item:((X509Data)object).getContent())if(item instanceof X509Certificate) {
                    final Key key=((X509Certificate)item).getPublicKey();
                    if(!(key instanceof RSAPublicKey) || ((RSAPublicKey)key).getModulus().bitLength()<2048)throw new KeySelectorException("RSA key below 2048 bits");
                    return new KeySelectorResult(){public Key getKey(){return key;}};
                }
            }
            throw new KeySelectorException("Missing signing certificate");
        }
    }
    public static void main(String[] args) throws Exception {
        if(args.length!=1)throw new IllegalArgumentException("Expected WGT path");
        DocumentBuilderFactory builder=DocumentBuilderFactory.newInstance();builder.setNamespaceAware(true);
        builder.setFeature(XMLConstants.FEATURE_SECURE_PROCESSING,true);
        builder.setFeature("http://apache.org/xml/features/disallow-doctype-decl",true);
        builder.setFeature("http://xml.org/sax/features/external-general-entities",false);
        builder.setFeature("http://xml.org/sax/features/external-parameter-entities",false);
        final XMLSignatureFactory factory=XMLSignatureFactory.getInstance("DOM");
        try(final ZipFile archive=new ZipFile(args[0])) {
            for(String filename:new String[]{"author-signature.xml","signature1.xml"}) {
                ZipEntry entry=archive.getEntry(filename);
                if(entry==null || entry.getSize()>1024*1024)throw new SecurityException("Missing/oversized signature");
                Document document;
                try(InputStream input=archive.getInputStream(entry)){document=builder.newDocumentBuilder().parse(input);}
                Set<String> ids=new HashSet<String>();NodeList elements=document.getElementsByTagNameNS("*","*");
                for(int i=0;i<elements.getLength();i++) {
                    Element element=(Element)elements.item(i);
                    if(element.hasAttribute("Id")) {
                        if(!ids.add(element.getAttribute("Id")))throw new SecurityException("Duplicate signature property ID");
                        element.setIdAttribute("Id",true);
                    }
                }
                NodeList signatures=document.getElementsByTagNameNS(XMLSignature.XMLNS,"Signature");
                if(signatures.getLength()!=1)throw new SecurityException("Expected exactly one signature document");
                DOMValidateContext context=new DOMValidateContext(new Keys(),signatures.item(0));
                context.setProperty("org.jcp.xml.dsig.secureValidation",Boolean.TRUE);
                context.setURIDereferencer(new URIDereferencer(){public Data dereference(URIReference reference,XMLCryptoContext ctx) throws URIReferenceException {
                    String uri=reference.getURI();
                    if(uri!=null && uri.startsWith("#"))return factory.getURIDereferencer().dereference(reference,ctx);
                    try {
                        URI location=new URI(uri);String path=location.getPath();
                        if(location.isAbsolute() || location.getRawAuthority()!=null || location.getQuery()!=null || location.getFragment()!=null || path==null || path.startsWith("/") || path.contains("\\") || Arrays.asList(path.split("/")).contains(".."))throw new SecurityException("External/unsafe signature reference");
                        ZipEntry payload=archive.getEntry(path);
                        if(payload==null || payload.getSize()>64*1024*1024)throw new SecurityException("Missing/oversized signed entry");
                        return new OctetStreamData(archive.getInputStream(payload),uri,null);
                    } catch(Exception error){throw new URIReferenceException(error);}
                }});
                XMLSignature signature=factory.unmarshalXMLSignature(context);
                String method=signature.getSignedInfo().getSignatureMethod().getAlgorithm();
                if(!method.equals("http://www.w3.org/2001/04/xmldsig-more#rsa-sha256") && !method.equals("http://www.w3.org/2001/04/xmldsig-more#rsa-sha512"))throw new SecurityException("Unsupported signature algorithm");
                if(!signature.validate(context))throw new SecurityException("Cryptographic verification failed: "+filename);
            }
        }
        System.out.println("Both XML signatures and references verified cryptographically; device certificate trust is separate");
    }
}
