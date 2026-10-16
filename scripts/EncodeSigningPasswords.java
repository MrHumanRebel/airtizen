/* Use the SDK's own inline password format; never depend on OS secret storage. */
import org.tizen.common.util.CipherUtil;
public final class EncodeSigningPasswords {
    public static void main(String[] args) throws Exception {
        for(String name:new String[]{"TIZEN_CERT_PASSWORD","TIZEN_DISTRIBUTOR_PASSWORD"}) {
            String password=System.getenv(name);
            if(password==null || password.isEmpty())throw new IllegalArgumentException("Missing signing password");
            String encoded=CipherUtil.getEncryptedString(password);
            if(!password.equals(CipherUtil.getDecryptedString(encoded)))throw new SecurityException("SDK password encoding failed");
            // Captured privately by prepare-signing-profile.py, never logged.
            System.out.println(encoded);
        }
    }
}
