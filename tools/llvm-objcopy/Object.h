//===- Object.h -------------------------------------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_OBJECT_H
#define LLVM_OBJCOPY_OBJECT_H

#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/FileOutputBuffer.h"

#include <memory>
#include <set>

class Segment;
class SectionBase;

class SectionTableRef {
private:
  llvm::ArrayRef<std::unique_ptr<SectionBase>> Sections;

public:
  SectionTableRef(llvm::ArrayRef<std::unique_ptr<SectionBase>> Secs)
      : Sections(Secs) {}
  SectionTableRef(const SectionTableRef &) = default;

  SectionBase *getSection(uint16_t Index, llvm::Twine ErrMsg);

  template <class T>
  T *getSectionOfType(uint16_t Index, llvm::Twine IndexErrMsg,
                      llvm::Twine TypeErrMsg);
};

class SectionBase {
public:
  llvm::StringRef Name;
  Segment *ParentSegment = nullptr;
  uint64_t HeaderOffset;
  uint64_t OriginalOffset;
  uint32_t Index;

  uint64_t Addr = 0;
  uint64_t Align = 1;
  uint32_t EntrySize = 0;
  uint64_t Flags = 0;
  uint64_t Info = 0;
  uint64_t Link = llvm::ELF::SHN_UNDEF;
  uint64_t NameIndex = 0;
  uint64_t Offset = 0;
  uint64_t Size = 0;
  uint64_t Type = llvm::ELF::SHT_NULL;

  virtual ~SectionBase() {}
  virtual void initialize(SectionTableRef SecTable);
  virtual void finalize();
  template <class ELFT> void writeHeader(llvm::FileOutputBuffer &Out) const;
  virtual void writeSection(llvm::FileOutputBuffer &Out) const = 0;
};

class Segment {
private:
  struct SectionCompare {
    bool operator()(const SectionBase *Lhs, const SectionBase *Rhs) const {
      // Some sections might have the same address if one of them is empty. To
      // fix this we can use the lexicographic ordering on ->Addr and the
      // address of the actully stored section.
      if (Lhs->OriginalOffset == Rhs->OriginalOffset)
        return Lhs < Rhs;
      return Lhs->OriginalOffset < Rhs->OriginalOffset;
    }
  };

  std::set<const SectionBase *, SectionCompare> Sections;
  llvm::ArrayRef<uint8_t> Contents;

public:
  uint64_t Align;
  uint64_t FileSize;
  uint32_t Flags;
  uint32_t Index;
  uint64_t MemSize;
  uint64_t Offset;
  uint64_t PAddr;
  uint64_t Type;
  uint64_t VAddr;

  uint64_t OriginalOffset;
  Segment *ParentSegment = nullptr;

  Segment(llvm::ArrayRef<uint8_t> Data) : Contents(Data) {}
  const SectionBase *firstSection() const {
    if (!Sections.empty())
      return *Sections.begin();
    return nullptr;
  }
  void addSection(const SectionBase *sec) { Sections.insert(sec); }
  template <class ELFT> void writeHeader(llvm::FileOutputBuffer &Out) const;
  void writeSegment(llvm::FileOutputBuffer &Out) const;
};

class Section : public SectionBase {
private:
  llvm::ArrayRef<uint8_t> Contents;

public:
  Section(llvm::ArrayRef<uint8_t> Data) : Contents(Data) {}
  void writeSection(llvm::FileOutputBuffer &Out) const override;
};

// This is just a wraper around a StringTableBuilder that implements SectionBase
class StringTableSection : public SectionBase {
private:
  llvm::StringTableBuilder StrTabBuilder;

public:
  StringTableSection() : StrTabBuilder(llvm::StringTableBuilder::ELF) {
    Type = llvm::ELF::SHT_STRTAB;
  }

  void addString(llvm::StringRef Name);
  uint32_t findIndex(llvm::StringRef Name) const;
  void finalize() override;
  void writeSection(llvm::FileOutputBuffer &Out) const override;
  static bool classof(const SectionBase *S) {
    return S->Type == llvm::ELF::SHT_STRTAB;
  }
};

// Symbols have a st_shndx field that normally stores an index but occasionally
// stores a different special value. This enum keeps track of what the st_shndx
// field means. Most of the values are just copies of the special SHN_* values.
// SYMBOL_SIMPLE_INDEX means that the st_shndx is just an index of a section.
enum SymbolShndxType {
  SYMBOL_SIMPLE_INDEX = 0,
  SYMBOL_ABS = llvm::ELF::SHN_ABS,
  SYMBOL_COMMON = llvm::ELF::SHN_COMMON,
  SYMBOL_HEXAGON_SCOMMON = llvm::ELF::SHN_HEXAGON_SCOMMON,
  SYMBOL_HEXAGON_SCOMMON_2 = llvm::ELF::SHN_HEXAGON_SCOMMON_2,
  SYMBOL_HEXAGON_SCOMMON_4 = llvm::ELF::SHN_HEXAGON_SCOMMON_4,
  SYMBOL_HEXAGON_SCOMMON_8 = llvm::ELF::SHN_HEXAGON_SCOMMON_8,
};

struct Symbol {
  uint8_t Binding;
  SectionBase *DefinedIn = nullptr;
  SymbolShndxType ShndxType;
  uint32_t Index;
  llvm::StringRef Name;
  uint32_t NameIndex;
  uint64_t Size;
  uint8_t Type;
  uint64_t Value;

  uint16_t getShndx() const;
};

class SymbolTableSection : public SectionBase {
protected:
  std::vector<std::unique_ptr<Symbol>> Symbols;
  StringTableSection *SymbolNames = nullptr;

public:
  void setStrTab(StringTableSection *StrTab) { SymbolNames = StrTab; }
  void addSymbol(llvm::StringRef Name, uint8_t Bind, uint8_t Type,
                 SectionBase *DefinedIn, uint64_t Value, uint16_t Shndx,
                 uint64_t Sz);
  void addSymbolNames();
  const Symbol *getSymbolByIndex(uint32_t Index) const;
  void initialize(SectionTableRef SecTable) override;
  void finalize() override;
  static bool classof(const SectionBase *S) {
    return S->Type == llvm::ELF::SHT_SYMTAB;
  }
};

// Only writeSection depends on the ELF type so we implement it in a subclass.
template <class ELFT> class SymbolTableSectionImpl : public SymbolTableSection {
  void writeSection(llvm::FileOutputBuffer &Out) const override;
};

struct Relocation {
  const Symbol *RelocSymbol = nullptr;
  uint64_t Offset;
  uint64_t Addend;
  uint32_t Type;
};

template <class SymTabType> class RelocationSectionBase : public SectionBase {
private:
  SymTabType *Symbols = nullptr;
  SectionBase *SecToApplyRel = nullptr;

public:
  void setSymTab(SymTabType *StrTab) { Symbols = StrTab; }
  void setSection(SectionBase *Sec) { SecToApplyRel = Sec; }
  void initialize(SectionTableRef SecTable) override;
  void finalize() override;
};

template <class ELFT>
class RelocationSection : public RelocationSectionBase<SymbolTableSection> {
private:
  typedef typename ELFT::Rel Elf_Rel;
  typedef typename ELFT::Rela Elf_Rela;

  std::vector<Relocation> Relocations;

  template <class T> void writeRel(T *Buf) const;

public:
  void addRelocation(Relocation Rel) { Relocations.push_back(Rel); }
  void writeSection(llvm::FileOutputBuffer &Out) const override;

  static bool classof(const SectionBase *S) {
    if (S->Flags & llvm::ELF::SHF_ALLOC)
      return false;
    return S->Type == llvm::ELF::SHT_REL || S->Type == llvm::ELF::SHT_RELA;
  }
};

class SectionWithStrTab : public Section {
private:
  StringTableSection *StrTab = nullptr;

public:
  SectionWithStrTab(llvm::ArrayRef<uint8_t> Data) : Section(Data) {}
  void setStrTab(StringTableSection *StringTable) { StrTab = StringTable; }
  void initialize(SectionTableRef SecTable) override;
  void finalize() override;
  static bool classof(const SectionBase *S);
};

class DynamicSymbolTableSection : public SectionWithStrTab {
public:
  DynamicSymbolTableSection(llvm::ArrayRef<uint8_t> Data)
      : SectionWithStrTab(Data) {}
  static bool classof(const SectionBase *S) {
    return S->Type == llvm::ELF::SHT_DYNSYM;
  }
};

class DynamicSection : public SectionWithStrTab {
public:
  DynamicSection(llvm::ArrayRef<uint8_t> Data) : SectionWithStrTab(Data) {}
  static bool classof(const SectionBase *S) {
    return S->Type == llvm::ELF::SHT_DYNAMIC;
  }
};

class DynamicRelocationSection
    : public RelocationSectionBase<DynamicSymbolTableSection> {
private:
  llvm::ArrayRef<uint8_t> Contents;

public:
  DynamicRelocationSection(llvm::ArrayRef<uint8_t> Data) : Contents(Data) {}
  void writeSection(llvm::FileOutputBuffer &Out) const override;
  static bool classof(const SectionBase *S) {
    if (!(S->Flags & llvm::ELF::SHF_ALLOC))
      return false;
    return S->Type == llvm::ELF::SHT_REL || S->Type == llvm::ELF::SHT_RELA;
  }
};

template <class ELFT> class Object {
private:
  typedef std::unique_ptr<SectionBase> SecPtr;
  typedef std::unique_ptr<Segment> SegPtr;

  typedef typename ELFT::Shdr Elf_Shdr;
  typedef typename ELFT::Ehdr Elf_Ehdr;
  typedef typename ELFT::Phdr Elf_Phdr;

  void initSymbolTable(const llvm::object::ELFFile<ELFT> &ElfFile,
                       SymbolTableSection *SymTab, SectionTableRef SecTable);
  SecPtr makeSection(const llvm::object::ELFFile<ELFT> &ElfFile,
                     const Elf_Shdr &Shdr);
  void readProgramHeaders(const llvm::object::ELFFile<ELFT> &ElfFile);
  SectionTableRef readSectionHeaders(const llvm::object::ELFFile<ELFT> &ElfFile);

protected:
  StringTableSection *SectionNames = nullptr;
  SymbolTableSection *SymbolTable = nullptr;
  std::vector<SecPtr> Sections;
  std::vector<SegPtr> Segments;

  void writeHeader(llvm::FileOutputBuffer &Out) const;
  void writeProgramHeaders(llvm::FileOutputBuffer &Out) const;
  void writeSectionData(llvm::FileOutputBuffer &Out) const;
  void writeSectionHeaders(llvm::FileOutputBuffer &Out) const;

public:
  uint8_t Ident[16];
  uint64_t Entry;
  uint64_t SHOffset;
  uint32_t Type;
  uint32_t Machine;
  uint32_t Version;
  uint32_t Flags;

  Object(const llvm::object::ELFObjectFile<ELFT> &Obj);
  virtual size_t totalSize() const = 0;
  virtual void finalize() = 0;
  virtual void write(llvm::FileOutputBuffer &Out) const = 0;
  virtual ~Object() = default;
};

template <class ELFT> class ELFObject : public Object<ELFT> {
private:
  typedef std::unique_ptr<SectionBase> SecPtr;
  typedef std::unique_ptr<Segment> SegPtr;

  typedef typename ELFT::Shdr Elf_Shdr;
  typedef typename ELFT::Ehdr Elf_Ehdr;
  typedef typename ELFT::Phdr Elf_Phdr;

  void sortSections();
  void assignOffsets();

public:
  ELFObject(const llvm::object::ELFObjectFile<ELFT> &Obj) : Object<ELFT>(Obj) {}
  void finalize() override;
  size_t totalSize() const override;
  void write(llvm::FileOutputBuffer &Out) const override;
};

template <class ELFT> class BinaryObject : public Object<ELFT> {
private:
  typedef std::unique_ptr<SectionBase> SecPtr;
  typedef std::unique_ptr<Segment> SegPtr;

  uint64_t TotalSize;

public:
  BinaryObject(const llvm::object::ELFObjectFile<ELFT> &Obj)
      : Object<ELFT>(Obj) {}
  void finalize() override;
  size_t totalSize() const override;
  void write(llvm::FileOutputBuffer &Out) const override;
};
#endif
