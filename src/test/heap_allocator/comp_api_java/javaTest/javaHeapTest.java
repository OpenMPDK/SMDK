import org.junit.Test;

/**
 * @author Jens Wilke; created: 2015-01-07
 */
public class javaHeapTest {

  static Object memoryConsumer;
  static final int Mbyte = 1024 * 1024;

  byte[][] allocateByteArray(int _row, int _col) {
    byte[][] ba = new byte[_row][_col];
    // initialize with some data. not sure if it make a difference
    // this should avoid that there is no memory mapped in
    for (int i = 0; i < _row; i++) {
	    for (int j =0; j < _col; j++) {
		    ba[i][j] = 0;
	    }
    }
    return ba;
  }


  @Test
  public void testBaseline() {
    javaHeapUtil.printUsedMemory();
  }

  @Test
  public void testBaseline1() {
    javaHeapUtil.printUsedMemory();
  }

  @Test
  public void testBaseline2() {
    javaHeapUtil.printUsedMemory();
  }

  @Test
  public void test10MBytes() {
    memoryConsumer = allocateByteArray(Mbyte, 10);
    javaHeapUtil.printUsedMemory();
  }

  @Test
  public void test100MBytes() {
    memoryConsumer = allocateByteArray(Mbyte, 100);
    javaHeapUtil.printUsedMemory();
  }

  @Test
  public void test27MBytes() {
    memoryConsumer = allocateByteArray(Mbyte, 27);
    javaHeapUtil.printUsedMemory();
  }

  @Test
  public void test127MBytes() {
    memoryConsumer = allocateByteArray(Mbyte, 127);
    javaHeapUtil.printUsedMemory();
  }
  
  @Test
  public void test1GBytes() {
    memoryConsumer = allocateByteArray(Mbyte, 1024);
    javaHeapUtil.printUsedMemory();
  }
  
  @Test
  public void test4GBytes() {
    memoryConsumer = allocateByteArray(Mbyte, 1024 * 4);
    javaHeapUtil.printUsedMemory();
  }

  @Test
  public void testBaseline3() {
    javaHeapUtil.printUsedMemory();
  }

  @Test
  public void testBaseline4() {
    javaHeapUtil.printUsedMemory();
  }



}
