//package in.derros.jni;

import java.nio.file.FileSystems;


class javaJNITest {
    // Ensure library is only loaded once
    static {
        if (System.getProperty("os.name").startsWith("Windows")) {
            // Windows based
            try {
                System.load(
                    FileSystems.getDefault()
                            .getPath("./build/libjnitests.dll")  // Dynamic link
                            .normalize().toAbsolutePath().toString());
            } catch (UnsatisfiedLinkError e) {
                System.load(
                    FileSystems.getDefault()
                            .getPath("./build/libjnitests.lib")  // Static link
                            .normalize().toAbsolutePath().toString());
            }
        } else {
            // Unix based
            try {
                System.load(
                    FileSystems.getDefault()
                            .getPath("./build/libjnitests.so")  // Dynamic link
                            .normalize().toAbsolutePath().toString());
            } catch (UnsatisfiedLinkError e) {
                System.load(
                    FileSystems.getDefault()
                            .getPath("./build/libjnitests.a")  // Static link
                            .normalize().toAbsolutePath().toString());
            }
        }
    }

    private native void printMethod();
    private native boolean trueFalse();
    private native int power(int b, int e);
    private native byte[] returnAByteArray();
    private native String stringManipulator(String s, String[] s1);
    private native void heapAlloc(long size);
    private native void mmapHooking(long size, String prio);

    public void printUtil() { printMethod();  }
    public boolean boolTest() { return trueFalse();  }
    public int pow(int b, int e) { return power(b, e); }
    public byte[] testReturnBytes() { return returnAByteArray(); }
    public String manipulateStrings(String s, String[] s1) { return stringManipulator(s, s1);  }
    public void testMalloc(int size) { heapAlloc(size); }
    public void testMmap(long size, String prio) { mmapHooking(size, prio); }

    public static void main(String[] args) {
        javaJNITest jnitest = new javaJNITest();
        jnitest.printUtil();
        System.out.println(jnitest.boolTest() + "\n");
        System.out.println(jnitest.pow(2, 2) + "\n\n");
        byte[] bs = jnitest.testReturnBytes();
        for ( byte b : bs ) { System.out.println("A Byte is: " + b);  }
        System.out.println("THIS IS THE STRING MANIPULATOR!!");
        System.out.println(
                jnitest.manipulateStrings("asdfxvcbiojdasaisdf hello world,,,",
                args));

        try{
            // call malloc method
            Thread.sleep(1000);
            jnitest.testMalloc(1024 * 1024 * 4);

            // call mmap method
            Thread.sleep(1000);
            jnitest.testMmap(1024 * 1024 * 4, args[0]);

        }catch(InterruptedException e){
            e.printStackTrace();
        }
    }
}
