
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <functional>
#include <unordered_map>

#include <seqan/sequence.h>
#include <seqan/basic.h>
#include <seqan/file.h>
#include <seqan/find.h>
#include <seqan/seq_io.h>
#include <seqan/modifier.h>

#include <jellyfish/backtrace.hpp>
#include <jellyfish/sequence_parser.hpp>
#include <jellyfish/parse_read.hpp>
#include <jellyfish/mer_counting.hpp>
#include <jellyfish/misc.hpp>
#include <jellyfish/compacted_hash.hpp>

#include "tclap/CmdLine.h"
#include "affymetrix_utils.hpp"


using affymetrix::utils::AffyEntry;

using seqan::DnaString;


typedef std::tuple< std::string, double, double > record;


struct IdProbT{
  size_t id;
  double prob;
};

struct SeqProbT{
  std::string seq;
  double prob;
};

std::ostream& operator<< ( std::ostream& os, const std::vector<IdProbT>& p ) {
  for ( auto& e : p ) {
    os << "\t" << e.id << ", " << e.prob;
  }
  return os;
}

typedef std::unordered_map<std::string, double> NaiveKmerMapT;

NaiveKmerMapT readSeqFile ( const std::string& fname, uint32_t& merSize ) {
  NaiveKmerMapT ents;
  std::ifstream affyFile(fname);

  auto update = [&]( const std::string& k, double v ) -> void {
   auto kIt = ents.find(k);
   // If the k-mer already exists in the map
   if ( kIt != ents.end() ) { kIt->second += v; } else { ents[k] = v; }
  };

  std::string seq;
  double expression, bg;
  std::string line;
  size_t i = 0;
  size_t failCount = 0;

  while ( std::getline(affyFile, line) ) {
    std::stringstream ls(line);
    ls >> seq >> expression >> bg ;
    if ( i % 10000 == 0 ) { std::cerr << "i = " << i << "\n"; }
    if ( !ls.fail() ) {
      auto bIndex = 0;
      auto eIndex = seq.length() - (merSize + 1);

      while (bIndex <= eIndex) {
        update(seq.substr(bIndex, merSize), (expression / bg) );
        ++bIndex;
      }
    } else {
      failCount += 1;
    }

    ++i;
  }

  std::cerr << failCount << " lines could not be correctly parsed and were ignored\n";

  affyFile.close();
  return ents;
}

bool dumpMap ( const std::string& fname, const NaiveKmerMapT& kmerMap ) {

  std::ofstream ofile(fname);

  for ( const auto& kv : kmerMap ) {
    ofile << kv.first << "\t" << kv.second << "\n";
  }

  ofile.close();
}

int command1(int argc, char* argv[] ) {
  using std::string;
  try{
    TCLAP::CmdLine cmd("RNASeq!", ' ', "1.0");
    TCLAP::ValueArg<string> affyFileName("a", "affy", "affymetrix probes with intensities", true, "", "string");
    TCLAP::ValueArg<string> outputFileName("o", "output", "file to which output should be written", true, "", "string");
    TCLAP::ValueArg<uint32_t> merSize("m", "mersize", "size of k-mers to count", true, 10, "uint32_t");
    cmd.add(affyFileName);
    cmd.add(outputFileName);
    cmd.add(merSize);

    cmd.parse(argc, argv);
    auto entries = readSeqFile( affyFileName.getValue(), merSize.getValue() );
    std::cerr << "found " << entries.size() << " distinct kmers \n";
    dumpMap ( outputFileName.getValue(), entries );
  } catch (TCLAP::ArgException &e){
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
  }
}

template <typename KeyT, typename ValueT>
std::unordered_map<KeyT, ValueT> readIntoHash( const std::string& fname, bool reverse = false ) {
  std::unordered_map<KeyT, ValueT> m;
  std::ifstream ifile(fname.c_str());

  KeyT k{};
  ValueT v{};

  std::unordered_map< char, char > rc{ {'A','T'}, {'T','A'}, {'C','G'}, {'G','C'} };

  auto reverseComplement = [&]( const std::string& s ) {
    std::string rs( s.size(), ' ' );
    size_t i = 0;
    for ( const auto& c : s ) {
      rs[i] = rc[c];
      ++i;
    }
    return rs;
  };

  while ( ifile >> k >> v ) {
    k = reverse ? reverseComplement(k) : k;
    m[k] = v;
  }

  ifile.close();
  return m;
}


/** Uses C style file I/O because it really does seem to be
    that much faster! **/
std::vector<SeqProbT> readSeqs( const std::string& fname ) {
  std::vector<SeqProbT> s;

  std::ifstream ifile(fname.c_str(), std::ios_base::binary);

  uint64_t numLines=0;
  uint32_t probeLength=0;

  ifile.read( reinterpret_cast<char*>(&numLines), sizeof(numLines));
  ifile.read( reinterpret_cast<char*>(&probeLength), sizeof(probeLength));

  char cseq[255];// = new char[255];//std::string seq;
  float expression, bg;

  s.reserve(numLines);


  //size_t offset = + sizeof(numLines) + sizeof(probeLength);
  //offset += (numLines-100)*((sizeof(char)*probeLength) + sizeof(expression) + sizeof(bg));
  //ifile.seekg( offset );
  //numLines = 100;

  for (size_t i = 0; i < numLines; ++i ) {
    uint32_t si = 0;
    //ifile.read( reinterpret_cast<char*>(&si), sizeof(si) );
    //std::cout << "size = " << si << "\n";
    ifile.read( cseq, sizeof(char)*probeLength);
    std::string seq(cseq, probeLength);
    ifile.read( reinterpret_cast<char*>(&expression), sizeof(expression) );
    ifile.read( reinterpret_cast<char*>(&bg), sizeof(bg) );
    s.push_back( SeqProbT{seq, (expression/bg)} );
    assert( seq[probeLength-1] != ' ' );
    std::cout << "> " << i << ":" << expression << ":" << bg << "\n";
    std::cout << seq << std::endl;

    if ( i % 100000 == 0 ) { std::cerr << i << "\n"; }
  }

  ifile.close();
  return s;
}


int command2(int argc, char* argv[] ) {
  using std::string;
  try{
    TCLAP::CmdLine cmd("RNASeq!", ' ', "1.0");
    TCLAP::ValueArg<string> seqFileName("s", "seq", "affymetrix sequences", true, "", "string");
    TCLAP::ValueArg<string> affyFileName("a", "affy", "affymetrix kmers", true, "", "string");
    TCLAP::ValueArg<string> rnaFileName("r", "rna", "rna-seq kmers", true, "", "string");

    cmd.add(seqFileName);
    cmd.add(affyFileName);
    cmd.add(rnaFileName);


    cmd.parse(argc, argv);
    std::cerr << "Reading seqs\n";
    auto seqs = readSeqs(seqFileName.getValue());
    std::cerr << "Reading affy kmers\n";
    auto amap = readIntoHash<std::string, double>(affyFileName.getValue());
    std::cerr << "Reading rna kmers\n";
    auto rmap = readIntoHash<std::string, uint32_t>(rnaFileName.getValue(), true);

    std::unordered_map<std::string, std::tuple<uint32_t,double>> omap;
    double X(0), Y(0), EX(0), EY(0), EXY(0), EX2(0), EY2(0);
    size_t N(0);

    for ( auto& kv : rmap ){
      if ( amap.find(kv.first) != amap.end() ) {
        ++N;
        X = kv.second; Y = amap[kv.first];
        EX += X; EY += Y;

        for ( auto& s : seqs ) {
          if ( s.seq.find(kv.first) != std::string::npos ) {
            double percent = s.prob / kv.second;
          }
        }

        omap[kv.first] = std::make_tuple( kv.second, amap[kv.first] );
        std::cerr << "omap[" << kv.first << "] = (" << kv.second << "," << amap[kv.first]  << ")\n";
      }
    }

    EX /= N; EY /= N;
    for ( const auto& kv : omap ) {
      X = std::get<0>(kv.second) - EX;
      Y = std::get<1>(kv.second) - EY;
      EXY += X*Y; EX2 += X*X; EY2 += Y*Y;
    }
    std::cerr << "N = " << N << ", EXY = " << EXY << ", EX = " << EX << ", EY = " << EY << ", EX2 = " << EX2 << ", EY2 = " << EY2 << "\n";
    double pearsonR = (EXY) / (sqrt(EX2*EY2)+1e-20);
    std::cerr << "pearson's r = " << pearsonR << "\n";
  } catch (TCLAP::ArgException &e){
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
  }
}


template <typename hash_t, typename stream_t>
bool countKmersInTranscripts( const hash_t& hash, stream_t& inputStream ) {

  size_t ctr{0};
  std::unordered_set<std::string> geneIDs;

  auto merLen = hash.get_mer_len();
  std::cerr << "mer length is " << merLen << "\n";
  size_t queryCounter = 0;

  jellyfish::read_parser::read_t *read;
  while ( (read = inputStream.next_read()) ) {//}&& queryCounter < 10000 ) {
    std::string readStr( read->seq_s, std::distance(read->seq_s, read->seq_e)-1 );
    //readStr.erase( std::remove( readStr.begin(), readStr.end(), '\n' ), readStr.end() );
    auto newEnd  = std::remove( readStr.begin(), readStr.end(), '\n' );
    auto readLen = std::distance( readStr.begin(), newEnd );

    std::string header( read->header, read->hlen );
    auto splitPos = header.find('|');
    auto geneName = header.substr(1, splitPos-1);
    geneIDs.insert(geneName);
    ++ctr;

    size_t offset = 0;
    while ( offset < readStr.size() - merLen ) {
      auto mer = readStr.substr( offset, merLen );
      //std::cerr << mer << " -> " <<  hash[mer.c_str()] << "\n";
      ++offset;
    }

    if ( queryCounter % 1000 == 0 ) {
      std::cerr << "queryCounter = " << queryCounter << "\n";
    }

    ++queryCounter;
  }

  std::cerr << "Database contained " << ctr << " entries comprising " << geneIDs.size() << " distinct genes\n";

  return true;
}


int command3(int argc, char* argv[] ) {
  using std::string;
  try{
    TCLAP::CmdLine cmd("RNASeq!", ' ', "1.0");
    TCLAP::ValueArg<string> genesFile("g", "genes", "gene sequences", true, "", "string");
    TCLAP::ValueArg<string> hashFile("j", "jfhash", "jellyfish hash file", true, "", "string");
    //TCLAP::ValueArg<string> affyFileName("a", "affy", "affymetrix probes with intensities", true, "", "string");
    //TCLAP::ValueArg<string> outputFileName("o", "output", "file to which output should be written", true, "", "string");
    //TCLAP::ValueArg<uint32_t> merSize("m", "mersize", "size of k-mers to count", true, 10, "uint32_t");
    cmd.add(genesFile);
    cmd.add(hashFile);
    //cmd.add(affyFileName);
    //cmd.add(outputFileName);
    //cmd.add(merSize);

    cmd.parse(argc, argv);

    const char* fnames[] = { genesFile.getValue().c_str() };
    jellyfish::parse_read parser( fnames+0, fnames+1, 2000);
    jellyfish::parse_read::thread stream = parser.new_thread();

    mapped_file dbf( hashFile.getValue().c_str() );
    //dbf.random().will_need();
    char type[8];
    memcpy(type, dbf.base(), sizeof(type));

    if(!strncmp(type, jellyfish::raw_hash::file_type, sizeof(type))) {
      raw_inv_hash_query_t hash(dbf);
      countKmersInTranscripts( hash, stream );
    } else if(!strncmp(type, jellyfish::compacted_hash::file_type, sizeof(type))) {
      hash_query_t hash(dbf);
      countKmersInTranscripts( hash, stream );
    }


  } catch (TCLAP::ArgException &e){
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
  }
}


int main( int argc, char* argv[] ) {

  using std::string;

  try {

    std::unordered_map<std::string, std::function<int(int, char*[])>> cmds({ {"cmd1", command1}, {"cmd2", command2}, {"cmd3", command3} });

    // TCLAP::CmdLine cmd("RNASeq!", ' ', "1.0");
    // TCLAP::SwitchArg cmd1("x", "xx", "cmd1", false);
    // TCLAP::SwitchArg cmd2("y", "yy", "cmd2", false);

    // std::vector<TCLAP::Arg*> cmds;
    // cmds.push_back(&cmd1);
    // cmds.push_back(&cmd2);

    // cmd.xorAdd( cmds );

    // cmd.parse(argc, argv);
    char** argv2 = new char*[argc-1];
    argv2[0] = argv[0];
    std::copy_n( &argv[2], argc-2, &argv2[1] );
    cmds[ argv[1] ](argc-1, argv2);
    delete[] argv2;

  } catch (TCLAP::ArgException &e) {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
  }


return 0;
}