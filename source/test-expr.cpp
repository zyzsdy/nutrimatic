#include "index.h"
#include "index-builder.h"
#include "search.h"
#include "expr.h"
#include "unicode.h"

#include "fst/concat.h"

#include <algorithm>
#include <string>
#include <vector>

#include <stdio.h>
#include <stdlib.h>

using namespace fst;

static void Fail(const char* message) {
  fprintf(stderr, "FAIL: %s\n", message);
  exit(1);
}

static void TestIndex(const char *expr, const char *yes, const char *no) {
  // Write index

  FILE *fp = fopen("test-expr.index", "wb");
  if (fp == NULL) {
    fprintf(stderr, "FAIL: can't write test-expr.index\n");
    exit(1);
  }

  IndexMetadata metadata;
  metadata.unicode_version = UnicodeVersionArray();
  IndexWriter writer(fp, metadata);
  std::vector<SymbolString> chains;
  if (yes != NULL) chains.push_back(DecodeUtf8Strict(yes));
  if (no != NULL) chains.push_back(DecodeUtf8Strict(no));
  for (SymbolString& chain : chains) chain.push_back(kEnd);
  std::sort(chains.begin(), chains.end());
  SymbolString previous;
  for (const SymbolString& chain : chains) {
    size_t same = 0;
    while (same < previous.size() && same < chain.size() &&
           previous[same] == chain[same]) ++same;
    writer.Next(&chain, same, 1);
    previous = chain;
  }
  writer.Finish();
  fclose(fp);

  // Parse expression

  if (getenv("DEBUG_FST") != NULL) fprintf(stderr, "### [%s]\n", expr);

  StdVectorFst fst;
  const char *p = ParseExpr(expr, &fst, false);
  if (p == NULL || *p != '\0') {
    fprintf(stderr, "FAIL: can't parse \"%s\"\n", p ? p : expr);
    exit(1);
  }

  // Read index

  fp = fopen("test-expr.index", "rb");
  if (fp == NULL) {
    fprintf(stderr, "FAIL: can't open test-expr.index\n");
    exit(1);
  }

  IndexReader reader(fp);
  ExprFilter filter(fst);
  SearchDriver sd(&reader, &filter, filter.start(), 1e-6);
  sd.next();

  // Verify results

  if (sd.text == NULL && yes == NULL) {
    if (getenv("DEBUG_FST") != NULL) fprintf(stderr, "-> NULL (ok)\n");
    return;
  }

  if (sd.text == NULL) {
    fprintf(stderr, "FAIL: [%s] -> NULL (expected \"%s\")\n", expr, yes);
    exit(1);
  }

  if (yes == NULL) {
    fprintf(stderr, "FAIL: [%s] -> \"%s\" (expected NULL)\n", expr, sd.text);
    exit(1);
  }

  if (strcmp(yes, sd.text)) {
    fprintf(stderr, "FAIL: [%s] -> \"%s\" (expected \"%s\")\n", expr, sd.text, yes);
    exit(1);
  }

  if (getenv("DEBUG_FST") != NULL) fprintf(stderr, "-> \"%s\" (ok)\n", yes);

  if (sd.text != NULL) {
    double score = sd.score;
    sd.next();
    if (sd.text != NULL && sd.score >= score) {
      fprintf(stderr, "FAIL: [%s] -> \"%s\" (extra)\n", expr, sd.text);
      exit(1);
    }
  }

  fclose(fp);
  remove("test-expr.index");
}

static void TestUnicodeQuery(const char* expression, const char* corpus,
                             const char* expected) {
  FILE* fp = fopen("test-expr.index", "wb");
  if (fp == NULL) Fail("can't write Unicode test index");
  IndexMetadata metadata;
  metadata.unicode_version = UnicodeVersionArray();
  IndexWriter writer(fp, metadata);
  std::vector<SymbolString> chains =
      GenerateIndexChains(NormalizeCorpusText(corpus).symbols);
  std::sort(chains.begin(), chains.end());
  SymbolString previous;
  for (const SymbolString& chain : chains) {
    size_t same = 0;
    while (same < previous.size() && same < chain.size() &&
           previous[same] == chain[same]) ++same;
    writer.Next(&chain, same, 1);
    previous = chain;
  }
  writer.Finish();
  fclose(fp);

  fp = fopen("test-expr.index", "rb");
  if (fp == NULL) Fail("can't read Unicode test index");
  IndexReader reader(fp);
  StdVectorFst parsed;
  ExprError error;
  const ExprCompileContext context{reader.alphabet(), {}};
  if (!CompileExpr(expression, context, &parsed, &error)) {
    fprintf(stderr, "FAIL: Unicode parse at %zu: %s\n", error.byte_offset,
            error.message.c_str());
    exit(1);
  }
  ExprFilter filter(parsed);
  SearchDriver driver(&reader, &filter, filter.start(), 0);
  driver.next();
  if (driver.text == NULL || strcmp(driver.text, expected) != 0) {
    fprintf(stderr, "FAIL: Unicode query [%s] -> [%s], expected [%s]\n",
            expression, driver.text ? driver.text : "NULL", expected);
    exit(1);
  }
  fclose(fp);
  remove("test-expr.index");
}

static void TestUnicodeErrors() {
  const std::vector<Symbol> alphabet = {EncodeScalar(U'\u4E00'),
                                        EncodeScalar(U'\u9FA5')};
  const ExprCompileContext context{alphabet, {}};
  StdVectorFst parsed;
  ExprError error;
  if (CompileExpr("[\xE4\xB8\x80-\xE9\xBE\xA5]", context, &parsed, &error))
    Fail("non-ASCII range must fail");
  if (error.message.find("non-ASCII") == std::string::npos)
    Fail("non-ASCII range error category");
  if (CompileExpr("\xCE\xB1", context, &parsed, &error))
    Fail("Greek literal must fail");
  if (error.byte_offset != 0 || error.message.find("U+03B1") == std::string::npos)
    Fail("Greek error offset and code point");
}

int main(int argc, char *argv[]) {
  TestIndex(
      "foo&bar",
      NULL,
      " ");

  TestIndex(
      "\"(((((m?o)?c)?h)?i)t?)_(h(a(t(o(ry?)?)?)?)?)?&_{5,}\" ",
      "chitchat ",
      "itch ");

  TestIndex(
      "(\"<(-may)?(-sit)?(tit)?(ble)?(com)?(iks)?(ial)?(im-b)?(-mon)?>\"&_{18}) ",
      "mayim bialiks sitcom ",
      "mayim bialiks common ");

  TestIndex(
      "([aehimnprsw]*&_*a_*&_*e_*&_*h_*&_*i_*&_*m_*&_*n_*&_*p_*&_*r_*&_*s_*&_*w_*) ",
      "new hampshire ",
      "minesweeper ship ");

  TestIndex(
      "<eelqsuuu> ",
      "equuleus ",
      "equus ");

  TestIndex(
      "(c?h?a?r?m?&____)(e?l?t?o?n?&____)(c?h?e?s?t?&____)(o?n?e?&__) ",
      "charlton heston ",
      "charmton heston ");

  TestIndex(
      "(<(cerb)?(ecto)?(lonm)?(ddog)?(fblo)?(iero)?(skey)?(ells)?(dwhi)?(atra)?(subj)?(odan)?(thel)?>&_{24}) ",
      "subject of blood and whiskey ",
      "subject of blood and whisubj ");

  TestIndex(
      "\"<(cs)(dy)(er)(i)(mo)(n)(th)(__?)>\" ",
      "thermodynamics ",
      "thermodyanmics ");

  TestIndex(
      "(<waterhegm>&_*w_*a_*t_*e_*r_*) ",
      "wheat germ ",
      "merge what ");

  TestIndex(
      "<het><ral><seg><tan><rut><bla><oody><afl><ndi><cin><awe><ter> ",
      "the largest natural body of land in ice water ",
      "the largest natural body of water in iceland ");

  TestUnicodeQuery("\xE4\xB8\xAD\xE5\x9B\xBD",
                   "\xE4\xB8\xAD\xE5\x9B\xBD\xE7\xA7\x91\xE5\xAD\xA6\xE9\x99\xA2",
                   "\xE4\xB8\xAD\xE5\x9B\xBD");
  TestUnicodeQuery("[\xE4\xB8\xAD\xE8\x8B\xB1]\xE6\x96\x87",
                   "\xE4\xB8\xAD\xE6\x96\x87", "\xE4\xB8\xAD\xE6\x96\x87");
  TestUnicodeQuery("\xC3\x9F{2}", "ssss", "ssss");
  TestUnicodeErrors();

  return 0;
}
