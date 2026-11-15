// Exercises the JVM under Barrique: JIT (hot loop), GC, threads, String/regex,
// java.util.zip, MessageDigest, file I/O. Prints RESULT: ok when all match.
import java.util.*; import java.util.concurrent.*; import java.util.zip.*;
import java.security.MessageDigest; import java.nio.file.*;
public class Probe {
    public static void main(String[] a) throws Exception {
        boolean ok = true;
        System.out.println("java " + System.getProperty("java.version") + " " + System.getProperty("os.arch"));
        long t0 = System.nanoTime(), s = 0;
        for (int i = 0; i < 200_000_000; i++) s += (i ^ (i >>> 3)) % 7;
        System.out.println("jit loop " + s + " " + (System.nanoTime() - t0) / 1_000_000 + " ms");
        ok &= s == 599999994L;
        List<int[]> junk = new ArrayList<>();
        for (int i = 0; i < 2000; i++) { junk.add(new int[50_000]); if (junk.size() > 100) junk.clear(); }
        ExecutorService ex = Executors.newFixedThreadPool(8);
        List<Future<Long>> fs = new ArrayList<>();
        for (int t = 0; t < 32; t++) { final int k = t; fs.add(ex.submit(() -> { long r = 0; for (int i = 0; i < 1_000_000; i++) r += i % (k + 1); return r; })); }
        long tot = 0; for (Future<Long> f : fs) tot += f.get(); ex.shutdown();
        System.out.println("threads " + tot);
        StringBuilder sb = new StringBuilder(); for (int i = 0; i < 10000; i++) sb.append("barrique "); byte[] data = sb.toString().getBytes();
        ByteArrayOutputStreamHolder h = new ByteArrayOutputStreamHolder();
        try (GZIPOutputStream g = new GZIPOutputStream(h.out)) { g.write(data); }
        GZIPInputStream gi = new GZIPInputStream(new java.io.ByteArrayInputStream(h.out.toByteArray())); java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream(); byte[] buf = new byte[4096]; int n; while ((n = gi.read(buf)) > 0) bo.write(buf, 0, n); byte[] back = bo.toByteArray();
        ok &= Arrays.equals(data, back);
        byte[] d = MessageDigest.getInstance("SHA-256").digest(data);
        StringBuilder hx = new StringBuilder(); for (int i = 0; i < 8; i++) hx.append(String.format("%02x", d[i])); System.out.println("sha256 " + hx);
        Path p = Files.createTempFile("probe", ".txt"); Files.write(p, "hello".getBytes()); ok &= new String(Files.readAllBytes(p)).equals("hello"); Files.delete(p);
        ok &= "a1b22c333".replaceAll("[0-9]+", "#").equals("a#b#c#");
        System.out.println("RESULT: " + (ok ? "ok" : "FAILED"));
    }
    static class ByteArrayOutputStreamHolder { java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream(); }
}
