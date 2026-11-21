// The text of an input and what a keyboard does to it (backend/text_edit.h): it is only text, with a caret and a
// selection, and it is UTF-8 that never gets cut in the middle of a letter.
#include <gtest/gtest.h>

#include "backend/text_edit.h"

namespace {
using kura::rt::TextEdit;

TEST(TextEdit, StartsWithTheCaretAtTheEnd) {
  const TextEdit edit("abc");
  EXPECT_EQ(edit.GetCaret(), 3U);
  EXPECT_FALSE(edit.HasSelection());
}

TEST(TextEdit, InsertsAtTheCaret) {
  TextEdit edit("ac");
  edit.Left();
  EXPECT_TRUE(edit.Insert("b"));
  EXPECT_EQ(edit.GetText(), "abc");
  EXPECT_EQ(edit.GetCaret(), 2U);
  EXPECT_FALSE(edit.Insert(""));
}

TEST(TextEdit, TypingReplacesTheSelection) {
  TextEdit edit("hello");
  edit.SelectAll();
  EXPECT_EQ(edit.SelectedText(), "hello");
  EXPECT_TRUE(edit.Insert("bye"));
  EXPECT_EQ(edit.GetText(), "bye");
  EXPECT_FALSE(edit.HasSelection());
}

TEST(TextEdit, BackspaceAndDelete) {
  TextEdit edit("abc");
  EXPECT_TRUE(edit.Backspace());
  EXPECT_EQ(edit.GetText(), "ab");
  edit.Home();
  EXPECT_FALSE(edit.Backspace());
  EXPECT_TRUE(edit.Delete());
  EXPECT_EQ(edit.GetText(), "b");
  edit.End();
  EXPECT_FALSE(edit.Delete());
}

TEST(TextEdit, DeletingASelectionDeletesOnlyThat) {
  TextEdit edit("abcd");
  edit.Home();
  edit.Right();
  edit.Right(true);
  edit.Right(true);
  EXPECT_EQ(edit.SelectedText(), "bc");
  EXPECT_TRUE(edit.Backspace());
  EXPECT_EQ(edit.GetText(), "ad");
  EXPECT_EQ(edit.GetCaret(), 1U);
}

TEST(TextEdit, AnArrowCollapsesASelectionToItsEdge) {
  TextEdit edit("abcd");
  edit.Home();
  edit.Right(true);
  edit.Right(true);
  edit.Left();
  EXPECT_FALSE(edit.HasSelection());
  EXPECT_EQ(edit.GetCaret(), 0U);

  edit.Right(true);
  edit.Right(true);
  edit.Right();
  EXPECT_EQ(edit.GetCaret(), 2U);
}

TEST(TextEdit, WordsAreWhatControlMovesBy) {
  TextEdit edit("one two  three");
  edit.Left(false, true);
  EXPECT_EQ(edit.GetCaret(), 9U);
  edit.Left(false, true);
  EXPECT_EQ(edit.GetCaret(), 4U);
  edit.Right(false, true);
  EXPECT_EQ(edit.GetCaret(), 7U);
  edit.End();
  EXPECT_TRUE(edit.Backspace(true));
  EXPECT_EQ(edit.GetText(), "one two  ");
  edit.Home();
  EXPECT_TRUE(edit.Delete(true));
  EXPECT_EQ(edit.GetText(), " two  ");
}

TEST(TextEdit, LettersAreNeverCutInHalf) {
  TextEdit edit("日本語");
  edit.Left();
  EXPECT_EQ(edit.GetCaret(), 6U);
  EXPECT_TRUE(edit.Backspace());
  EXPECT_EQ(edit.GetText(), "日語");
  edit.SetCaret(1);  // in the middle of 日
  EXPECT_EQ(edit.GetCaret(), 0U);
  edit.End();
  EXPECT_TRUE(edit.Backspace());
  EXPECT_EQ(edit.GetText(), "日");
}

TEST(TextEdit, TheProgramCanChangeTheText) {
  TextEdit edit("abcdef");
  edit.SetText("ab");
  EXPECT_EQ(edit.GetCaret(), 2U);
  EXPECT_EQ(edit.GetAnchor(), 2U);
  edit.Home();
  edit.SetText("wxyz");
  EXPECT_EQ(edit.GetCaret(), 0U);
  edit.SetText("日");
  edit.End();
  edit.SetText("");
  EXPECT_EQ(edit.GetCaret(), 0U);
}

TEST(TextEdit, SingleLineTextKeepsNoBreaks) {
  EXPECT_EQ(TextEdit::Sanitize("a\r\nb\nc\rd\te"), "a b c d e");
  EXPECT_EQ(TextEdit::Sanitize(std::string("a\x01") + "b\x7F"), "ab");
  EXPECT_EQ(TextEdit::Sanitize("日本"), "日本");
}
}  // namespace
