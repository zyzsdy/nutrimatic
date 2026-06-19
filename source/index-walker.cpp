#include "index.h"

IndexWalker::IndexWalker(const IndexReader* reader, IndexReader::Node node,
                         std::uint64_t count)
    : reader_(reader) {
  (void)count;
  if (reader_ == nullptr) throw IndexFormatError("null index reader");
  stack_.resize(1);
  stack_size_ = 1;
  reader_->Children(node, kEpsilon, kPositionBreak, &stack_[0].choices);
  Next();
}

void IndexWalker::Next() {
  while (stack_size_ != 0 &&
         stack_[stack_size_ - 1].next ==
             stack_[stack_size_ - 1].choices.size()) {
    stack_[--stack_size_].choices.clear();
  }
  if (stack_size_ == 0) {
    chain = nullptr;
    same = 0;
    count = 0;
    return;
  }

  same = stack_size_ - 1;
  for (;;) {
    State& parent = stack_[stack_size_ - 1];
    const IndexReader::Choice choice = parent.choices[parent.next++];
    if (buffer_.size() < stack_size_) buffer_.resize(stack_size_);
    buffer_[stack_size_ - 1] = choice.symbol;
    if (choice.next == 0) {
      buffer_.resize(stack_size_);
      chain = &buffer_;
      count = choice.count;
      return;
    }

    if (++stack_size_ > stack_.size()) stack_.resize(stack_size_);
    State& child = stack_[stack_size_ - 1];
    child.next = 0;
    child.choices.clear();
    reader_->Children(choice.next, kEpsilon, kPositionBreak, &child.choices);
    if (child.choices.empty())
      throw IndexFormatError("referenced index node has no children");
  }
}
