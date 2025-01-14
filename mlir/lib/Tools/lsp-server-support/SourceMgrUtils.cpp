//===--- SourceMgrUtils.cpp - SourceMgr LSP Utils -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/lsp-server-support/SourceMgrUtils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Path.h"
#include <mlir/Tools/lsp-server-support/Logging.h>
#include <optional>

using namespace mlir;
using namespace mlir::lsp;

//===----------------------------------------------------------------------===//
// Utils
//===----------------------------------------------------------------------===//

/// Find the end of a string whose contents start at the given `curPtr`. Returns
/// the position at the end of the string, after a terminal or invalid character
/// (e.g. `"` or `\0`).
static const char *lexLocStringTok(const char *curPtr) {
  while (char c = *curPtr++) {
    // Check for various terminal characters.
    if (StringRef("\"\n\v\f").contains(c))
      return curPtr;

    // Check for escape sequences.
    if (c == '\\') {
      // Check a few known escapes and \xx hex digits.
      if (*curPtr == '"' || *curPtr == '\\' || *curPtr == 'n' || *curPtr == 't')
        ++curPtr;
      else if (llvm::isHexDigit(*curPtr) && llvm::isHexDigit(curPtr[1]))
        curPtr += 2;
      else
        return curPtr;
    }
  }

  // If we hit this point, we've reached the end of the buffer. Update the end
  // pointer to not point past the buffer.
  return curPtr - 1;
}

SMRange lsp::convertTokenLocToRange(SMLoc loc, StringRef identifierChars) {
  if (!loc.isValid())
    return SMRange();
  const char *curPtr = loc.getPointer();

  // Check if this is a string token.
  if (*curPtr == '"') {
    curPtr = lexLocStringTok(curPtr + 1);

    // Otherwise, default to handling an identifier.
  } else {
    // Return if the given character is a valid identifier character.
    auto isIdentifierChar = [=](char c) {
      return isalnum(c) || c == '_' || identifierChars.contains(c);
    };

    while (*curPtr && isIdentifierChar(*(++curPtr)))
      continue;
  }

  return SMRange(loc, SMLoc::getFromPointer(curPtr));
}

std::optional<std::string>
lsp::extractSourceDocComment(llvm::SourceMgr &sourceMgr, SMLoc loc) {
  // This is a heuristic, and isn't intended to cover every case, but should
  // cover the most common. We essentially look for a comment preceding the
  // line, and if we find one, use that as the documentation.
  if (!loc.isValid())
    return std::nullopt;
  int bufferId = sourceMgr.FindBufferContainingLoc(loc);
  if (bufferId == 0)
    return std::nullopt;
  const char *bufferStart =
      sourceMgr.getMemoryBuffer(bufferId)->getBufferStart();
  StringRef buffer(bufferStart, loc.getPointer() - bufferStart);

  // Pop the last line from the buffer string.
  auto popLastLine = [&]() -> std::optional<StringRef> {
    size_t newlineOffset = buffer.find_last_of('\n');
    if (newlineOffset == StringRef::npos)
      return std::nullopt;
    StringRef lastLine = buffer.drop_front(newlineOffset).trim();
    buffer = buffer.take_front(newlineOffset);
    return lastLine;
  };

  // Try to pop the current line.
  if (!popLastLine())
    return std::nullopt;

  // Try to parse a comment string from the source file.
  SmallVector<StringRef> commentLines;
  while (std::optional<StringRef> line = popLastLine()) {
    // Check for a comment at the beginning of the line.
    if (!line->starts_with("//"))
      break;

    // Extract the document string from the comment.
    commentLines.push_back(line->ltrim('/'));
  }

  if (commentLines.empty())
    return std::nullopt;
  return llvm::join(llvm::reverse(commentLines), "\n");
}

bool lsp::contains(SMRange range, SMLoc loc) {
  return range.Start.getPointer() <= loc.getPointer() &&
         loc.getPointer() < range.End.getPointer();
}

//===----------------------------------------------------------------------===//
// SourceMgrInclude
//===----------------------------------------------------------------------===//

Hover SourceMgrInclude::buildHover() const {
  Hover hover(range);
  {
    llvm::raw_string_ostream hoverOS(hover.contents.value);
    hoverOS << "`" << llvm::sys::path::filename(uri.file()) << "`\n***\n"
            << uri.file();
  }
  return hover;
}

void lsp::gatherIncludeFiles(llvm::SourceMgr &sourceMgr,
                             SmallVectorImpl<SourceMgrInclude> &includes) {
  Logger::info("Begin searIncludeFiles");
  for(unsigned int i = 1; i <= sourceMgr.getNumBuffers(); i++){
    Logger::info("Buffer {0} path {1} is_main {2}", i, sourceMgr.getMemoryBuffer(i)->getBufferIdentifier(), i == sourceMgr.getMainFileID());
  }
  for (unsigned i = 1, e = sourceMgr.getNumBuffers(); i < e; ++i) {
    // Check to see if this file was included by the main file.
    SMLoc includeLoc = sourceMgr.getBufferInfo(i + 1).IncludeLoc;
    if (!includeLoc.isValid() || sourceMgr.FindBufferContainingLoc(
                                     includeLoc) != sourceMgr.getMainFileID())
      continue;

    // Try to build a URI for this file path.
    auto *buffer = sourceMgr.getMemoryBuffer(i + 1);
    llvm::SmallString<256> path(buffer->getBufferIdentifier());
    llvm::sys::path::remove_dots(path, /*remove_dot_dot=*/true);
    if(!llvm::sys::path::is_absolute(path)){
      auto includerPath = sourceMgr.getMemoryBuffer(sourceMgr.FindBufferContainingLoc(includeLoc))->getBufferIdentifier();
      auto includerDir = llvm::sys::path::parent_path(includerPath);

      std::string absPath = (includerDir + "/" + path).str();
      path.assign(absPath);
    }

    llvm::Expected<URIForFile> includedFileURI = URIForFile::fromFile(path);
    if (!includedFileURI){
      // Without this expected is unchecked and server crashes
      handleAllErrors(includedFileURI.takeError(), [&](const llvm::ErrorInfoBase &EI) {

      });
      continue;
    }

    // Find the end of the include token.
    const char *includeStart = includeLoc.getPointer() - 2;
    while (*(--includeStart) != '\"')
      continue;

    // Push this include.
    SMRange includeRange(SMLoc::getFromPointer(includeStart), includeLoc);
    includes.emplace_back(*includedFileURI, Range(sourceMgr, includeRange));
  }
}
