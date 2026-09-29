package dev.personal.subtitle;
import android.content.Context;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
final class SecureKeys {
    private static final String ALIAS="subtitle.user.deepl";
    private static SecretKey key() throws Exception {
        KeyStore store=KeyStore.getInstance("AndroidKeyStore"); store.load(null);
        if(!store.containsAlias(ALIAS)) {
            KeyGenerator g=KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES,"AndroidKeyStore");
            g.init(new KeyGenParameterSpec.Builder(ALIAS,KeyProperties.PURPOSE_ENCRYPT|KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());
            g.generateKey();
        }
        return (SecretKey)store.getKey(ALIAS,null);
    }
    static void save(Context c,String secret) throws Exception {
        Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding"); cipher.init(Cipher.ENCRYPT_MODE,key());
        byte[] encrypted=cipher.doFinal(secret.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        c.getSharedPreferences("secrets",0).edit()
          .putString("blob",Base64.encodeToString(encrypted,Base64.NO_WRAP))
          .putString("iv",Base64.encodeToString(cipher.getIV(),Base64.NO_WRAP)).apply();
    }
    static String load(Context c) throws Exception {
        var prefs=c.getSharedPreferences("secrets",0); String blob=prefs.getString("blob","");
        if(blob.isEmpty()) return "";
        Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.DECRYPT_MODE,key(),new GCMParameterSpec(128,Base64.decode(prefs.getString("iv",""),Base64.NO_WRAP)));
        return new String(cipher.doFinal(Base64.decode(blob,Base64.NO_WRAP)),java.nio.charset.StandardCharsets.UTF_8);
    }
    static void clear(Context c) { c.getSharedPreferences("secrets",0).edit().clear().apply(); }
}
