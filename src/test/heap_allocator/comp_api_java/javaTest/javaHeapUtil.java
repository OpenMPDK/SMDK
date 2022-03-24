/**
 * @author Jens Wilke; created: 2015-01-07
 */
public class javaHeapUtil {

  static String extractTestName() {
    Exception e = new Exception();
    int idx = 1;
    StackTraceElement[] _stackTrace = e.getStackTrace();
    do {
      String n = _stackTrace[idx].getMethodName();
      idx++;
      if (n.startsWith("test")) {
        return _stackTrace[idx - 1].getMethodName();
      }
    } while (true);
  }

  public static void printUsedMemory() {
    String _testName = extractTestName();
    System.out.println(_testName + ": benchmark is requesting GC (record used memory)...");
    System.out.flush();
    try {
      Runtime.getRuntime().gc();
      Thread.sleep(55);
      Runtime.getRuntime().gc();
      Thread.sleep(55);
      Runtime.getRuntime().gc();
      Thread.sleep(55);
      Runtime.getRuntime().gc();
      Thread.sleep(55);
    } catch (Exception ignore) { }
    long _usedMem;
    long _total;
    long _total2;
    long _count = -1;
    // loop to get a stable reading, since memory may be resized between the method calls
    do {
      _count++;
      _total = Runtime.getRuntime().totalMemory();
      try {
        Thread.sleep(12);
      } catch (Exception ignore) { }
      long _free = Runtime.getRuntime().freeMemory();
      _total2 = Runtime.getRuntime().totalMemory();
      _usedMem = _total - _free;
    } while (_total != _total2);
    System.out.println(_testName + ": used=" + _usedMem + ", loopCount=" + _count + ", total=" + _total);
  }

}
