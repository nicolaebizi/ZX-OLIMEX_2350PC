#include "unity.h"
#include "ArrayInputStream.h"
#include "BufferedInputStream.h"

void setUp(void) {}
void tearDown(void) {}

// A stream that fails after a number of bytes, like an SD card being pulled
class FailingInputStream : public ArrayInputStream {
public:
  FailingInputStream(const uint8_t* buf, uint32_t length) : ArrayInputStream(buf, length) {}
  virtual int32_t read(uint8_t* buffer, const uint32_t length) {
    int32_t r = ArrayInputStream::read(buffer, length);
    return r <= 0 ? -2 : r;
  }
};

void test_buffered_reads_then_eof(void) {
  const uint8_t data[] = { 1, 2, 3 };
  ArrayInputStream is(data, sizeof(data));
  BufferedInputStream bis(&is);
  TEST_ASSERT_EQUAL(1, bis.readByte());
  TEST_ASSERT_EQUAL(2, bis.readByte());
  TEST_ASSERT_EQUAL(3, bis.readByte());
  // EOF must be reported, and keep being reported, rather than reading past the buffer
  for (int i = 0; i < 2000; ++i) {
    TEST_ASSERT_EQUAL(-1, bis.readByte());
  }
}

void test_buffered_spans_buffer_refill(void) {
  static uint8_t data[2500];
  for (uint32_t i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)i;
  ArrayInputStream is(data, sizeof(data));
  BufferedInputStream bis(&is);
  for (uint32_t i = 0; i < sizeof(data); ++i) {
    TEST_ASSERT_EQUAL((uint8_t)i, bis.readByte());
  }
  TEST_ASSERT_EQUAL(-1, bis.readByte());
}

void test_buffered_reports_error(void) {
  const uint8_t data[] = { 7 };
  FailingInputStream is(data, sizeof(data));
  BufferedInputStream bis(&is);
  TEST_ASSERT_EQUAL(7, bis.readByte());
  TEST_ASSERT_EQUAL(-2, bis.readByte());
  TEST_ASSERT_EQUAL(-2, bis.readByte());
}

void test_skip_returns_length(void) {
  const uint8_t data[] = { 1, 2, 3, 4, 5 };
  ArrayInputStream is(data, sizeof(data));
  TEST_ASSERT_EQUAL(3, is.skip(3));
  TEST_ASSERT_EQUAL(4, is.readByte());
  // Skipping past the end returns what was skipped
  TEST_ASSERT_EQUAL(1, is.skip(10));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_buffered_reads_then_eof);
  RUN_TEST(test_buffered_spans_buffer_refill);
  RUN_TEST(test_buffered_reports_error);
  RUN_TEST(test_skip_returns_length);
  return UNITY_END();
}
