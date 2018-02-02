// amr_report.cpp

#undef NDEBUG 
#include "common.inc"

#include "common.hpp"
#include "graph.hpp"
using namespace Common_sp;



namespace 
{


struct Fam : Tree::TreeNode  
// Table PROTEUS.VF..FAM
{
  string id; 
  string genesymbol;
  string familyName; 
  bool reportable {false}; 

  // HMM
  string hmm; 
    // May be empty()
  double tc1 {NAN}; 
  double tc2 {NAN}; 


  Fam (Tree &tree,
       Fam* parent_arg,
       const string &id_arg,
       const string &genesymbol_arg,
       const string &hmm_arg,
       double tc1_arg,
       double tc2_arg,
       const string &familyName_arg,
       bool reportable_arg)
    : Tree::TreeNode (tree, parent_arg)
    , id (id_arg)
    , genesymbol (genesymbol_arg)
    , familyName (familyName_arg)
    , reportable (reportable_arg)
    , hmm (hmm_arg)
    , tc1 (tc1_arg)
    , tc2 (tc2_arg)
    { if (genesymbol == "-")
        genesymbol. clear ();
      if (hmm == "-")
        hmm. clear ();
      if (hmm. empty () != ! tc1)
      { cout << id << ' ' << hmm << ' ' << tc1 << endl;
        ERROR;
      }
      ASSERT (hmm. empty () == ! tc2); 
    //IMPLY (! hmm. empty (), tc2 > 0);
      if (familyName == "NULL")
        familyName. clear ();
      ASSERT (tc2 >= 0);        
      ASSERT (tc2 <= tc1);
    }
  explicit Fam (Tree &tree)
    : Tree::TreeNode (tree, nullptr)
    {}
  void saveContent (ostream &os) const final
    { os << hmm << " " << tc1 << " " << tc2 << " " << familyName << " " << reportable; }


  string getName () const final
    { return id; }
  const Fam* getHmmFam () const
    // Return: most specific HMM
    { const Fam* f = this;
      while (f && f->hmm. empty ())
        f = static_cast <const Fam*> (f->getParent ());
      return f;
    }
};


map<string/*famId*/,const Fam*> famId2fam;
map<string/*hmm*/,const Fam*> hmm2fam;



struct HmmAlignment : Root  
// Query: AMR HMM
{
  string sseqid; 
  double score1 {NAN}; 
  double score2 {NAN}; 
    // May be different from max(Domain::score)
  const Fam* fam {nullptr};
    // Query
//ali_from, ali_to ??
  
  
  explicit HmmAlignment (const string &line)
    {
	    istringstream iss (line);
	    string hmm, dummy;
	    //                                                        --- full sequence ---  --- best 1 domain --
	    //     target name  accession  query name     accession   E-value  score     bias     E-value  score  bias   exp reg clu  ov env dom rep inc description of target
	    iss >> sseqid >>    dummy      >> hmm      >> dummy >>    dummy >> score1 >> dummy >> dummy >> score2;
	    ASSERT (score1 > 0);
	    ASSERT (score2 > 0)
	    fam = hmm2fam [hmm];
    }
  void saveText (ostream &os) const final
    { os << sseqid << ' ' << score1 << ' ' << score2 << ' ' << (fam ? fam->hmm : string ()); }
      
      
  bool good () const
    { return    fam 
             && ! fam->hmm. empty ()
             && score1 >= fam->tc1
             && score2 >= fam->tc2
           //&& fam->reportable
             ; 
    }
private:
  bool betterEq (const HmmAlignment &other,
                 unsigned char criterion) const
    // Reflexive
    // For one sseqid: one HmmAlignment is better than all others
    { ASSERT (good ());
      ASSERT (other. fam);
      if (sseqid != other. sseqid)
        return false;
      switch (criterion)
      {
        case 0: return fam->descendantOf (other. fam); 
        case 1: 
          {
            LESS_PART (other, *this, score1);
          //return score2 >= other. score2;  // GP-16770
            LESS_PART (other, *this, fam->tc1);
            LESS_PART (*this, other, fam->hmm);  // Tie resolution
            return true;
          }
        default: ERROR;
      }
      return false;
    }
public:
  bool better (const HmmAlignment &other,
               unsigned char criterion) const
    { return betterEq (other, criterion) && ! other. betterEq (*this, criterion); }


  typedef  pair<string/*sseqid*/,string/*FAM.id*/>  Pair;
  
  
  struct Domain  
  {
    double score {0};
    size_t hmmLen {0};
    size_t hmmStart {0}; 
    size_t hmmStop {0};
    size_t seqLen {0}; 
    size_t seqStart {0};
    size_t seqStop {0}; 
    
    Domain (double score_arg,
            size_t hmmLen_arg,
            size_t hmmStart_arg,
            size_t hmmStop_arg,
            size_t seqLen_arg,
            size_t seqStart_arg,
            size_t seqStop_arg)
      : score (score_arg)
      , hmmLen (hmmLen_arg)
      , hmmStart (hmmStart_arg)
      , hmmStop (hmmStop_arg)
      , seqLen (seqLen_arg)
      , seqStart (seqStart_arg)
      , seqStop (seqStop_arg)
      { ASSERT (hmmStart < hmmStop);
        ASSERT (seqStart < seqStop);
        ASSERT (hmmStop <= hmmLen);
        ASSERT (seqStop <= seqLen);
      }
    Domain ()
      {} 
  };
};



double ident_min = NAN;
double cover_min = NAN;
bool cdsExist = false;
bool print_fam = false;

bool reportPseudo = false; 
const string stopCodonS ("[stop]");
const string frameShiftS ("[frameshift]");



struct CDS 
{
  string contig;
  size_t start {0};
  size_t stop {0};
  bool strand {false};
  
  CDS (const string &contig_arg,
       size_t start_arg,
       size_t stop_arg,
       bool strand_arg)
    : contig (contig_arg)
    , start (start_arg)
    , stop (stop_arg)
    , strand (strand_arg)
    { ASSERT (! contig. empty ());
      ASSERT (start < stop); 
    }
  CDS ()
    {} 
    
  bool operator< (const CDS& other) const
    { LESS_PART (*this, other, contig)
      LESS_PART (*this, other, start)
      LESS_PART (*this, other, stop)
      LESS_PART (*this, other, strand)
      return false;
    }
};



struct BlastAlignment : Root 
{
  // BLAST alignment
  size_t length {0}, nident {0}  // aa
       ,    refStart {0},    refEnd {0},    refLen {0}
       , targetStart {0}, targetEnd {0}, targetLen {0};
    // Positions are 0-based
    // targetStart < targetEnd

  // target    
  string targetName; 
  bool targetProt {true};
    // false <=> DNA
  bool targetStrand {true}; 
    // false <=> negative
  size_t targetAlign {0};
  size_t targetAlign_aa {0};
  bool partialDna {false};
  bool stopCodon {false}; 
  bool frameShift {false};
  
  // Reference (AMR) protein
  bool refStrand {true};
  long gi {0};  
    // 0 <=> HMM method
  string accessionProt; 
  string accessionDna;  
  size_t part {1};    
    // >= 1
    // <= parts
  size_t parts {1};   // > 1: not used ??
    // >= 1
  // Table FAM
  string famId;  
  string gene;   
    // FAM.class  
  string mechanism; 
    // FAM.resistance
  const bool mutant {false}; 
  string product;  

  vector<CDS> cdss;
  
  static constexpr size_t mismatchTail_aa = 10;  // PAR
  size_t mismatchTailTarget {0};


  BlastAlignment (const string &line,
                  bool targetProt_arg)
    : targetProt (targetProt_arg)
    {
	    istringstream iss (line);
      string refName, targetSeq;
	    iss >> targetName >> refName >> length >> nident >> targetStart >> targetEnd >> targetLen >> refStart >> refEnd >> refLen >> targetSeq;
	  // format:  qseqid      sseqid    length    nident         qstart         qend         qlen      sstart      send      slen         sseq
    // blastp:  ...         ...          663       169              2          600          639           9       665       693          ...
    // blastx:  ...         ...          381       381          13407        14549        57298           1       381       381          ...
    // blastn:  ...         ...          733       733          62285        63017        88215         105       837       837          ...
	    ASSERT (! targetSeq. empty ());	    

      // refName	    
	    product       = rfindSplit (refName, '|'); 
	  //mutant        = false;  // str2<int> (rfindSplit (refName, '|'));  // ??
	    mechanism     = rfindSplit (refName, '|');
	    gene          = rfindSplit (refName, '|');  // Reportable_vw.class
	    famId         = rfindSplit (refName, '|');  // Reportable_vw.fam
	    parts         = (size_t) str2<int> (rfindSplit (refName, '|'));
	    part          = (size_t) str2<int> (rfindSplit (refName, '|'));
	    accessionDna  = rfindSplit (refName, '|');
	    accessionProt = rfindSplit (refName, '|');
	    gi            = str2<long> (refName);
	    ASSERT (gi > 0);
	    	    
	    replace (product, '_', ' ');
	    
	    ASSERT (refStart != refEnd);
	    refStrand = refStart < refEnd;  
	    ASSERT (refStrand);  // ??
	    if (! refStrand)
	      swap (refStart, refEnd);

	    ASSERT (targetStart != targetEnd);
	    targetStrand = targetStart < targetEnd;  
	    if (! targetStrand)
	      swap (targetStart, targetEnd);
	      
	    ASSERT (refStart >= 1);
	    ASSERT (targetStart >= 1);
	    ASSERT (refStart < refEnd);
	    ASSERT (targetStart < targetEnd);
	    refStart--;
	    targetStart--;

	    partialDna = false;
	    constexpr size_t mismatchTailDna = 10;  // PAR
	    if (! targetProt && targetEnd - targetStart >= 30)  // PAR, PD-671
	    {
	           if (refStart > 0      && targetTail (true)  <= mismatchTailDna)  partialDna = true;
	      else if (refEnd   < refLen && targetTail (false) <= mismatchTailDna)  partialDna = true;
	    }

      setTargetAlign ();	    
	    
	    if (contains (targetSeq, "*"))  
	      stopCodon  =  true;
	  //frameShift = contains (targetSeq, "/");  // Needs "blastall -p blastx ... "
	    if (! targetProt && (targetEnd - targetStart) % 3 != 0)
  	    frameShift = true;	  
	  	  
	    // For BLASTX
	  	// PD-1280
	    if (   ! targetProt 
	        && refStart == 0 
	        && charInSet (targetSeq [0], "LIV") 
	        && nident < targetAlign_aa
	       )
	      nident++;
	    	    
	    mismatchTailTarget = mismatchTail_aa;
	    if (! targetProt)
	      mismatchTailTarget *= 3;
    }
  explicit BlastAlignment (const HmmAlignment& other)
    : targetName (other. sseqid)     
    , famId      (other. fam->id)   
    , gene       (other. fam->id)   
    , product    (other. fam->familyName)   
    { if (allele ())
        ERROR_MSG (famId + " " + gene);
    }
  void qc () const final
    {
      if (! qc_on)
        return;
	    ASSERT (! famId. empty ());
	    ASSERT (! gene. empty ());
	    ASSERT (part >= 1);
	    ASSERT (part <= parts);
	    ASSERT (! product. empty ());
	    ASSERT (refStrand);
	    IMPLY (targetProt, targetStrand);  
	    ASSERT (targetStart < targetEnd);
	    ASSERT (targetEnd <= targetLen);
	    ASSERT ((bool) gi == (bool) length);
	    ASSERT ((bool) gi == (bool) refLen);
	    ASSERT ((bool) gi == (bool) nident);
	    ASSERT ((bool) gi == ! accessionProt. empty ());
	    IMPLY (! gi, getFam () -> getHmmFam ());
	    IMPLY (accessionProt. empty (), accessionDna. empty ());
	    IMPLY (targetProt, ! partialDna);
	  //IMPLY (! targetProt, (targetEnd - targetStart) % 3 == 0);
	    ASSERT (targetAlign);
	    IMPLY (targetProt, targetAlign == targetAlign_aa);
	    IMPLY (! targetProt, targetAlign == 3 * targetAlign_aa);
	    ASSERT (nident <= targetAlign_aa);
	    IMPLY (! targetProt, cdss. empty ());
	    if (gi)
	    {
	      ASSERT (refStart < refEnd);
  	    ASSERT (nident <= refEnd - refStart);
  	    ASSERT (refEnd <= refLen);
  	    ASSERT (refEnd - refStart <= length);	    
  	    ASSERT (targetAlign_aa <= length);
	    }
    }
  void saveText (ostream& os) const final
    { // PD-736, PD-774, PD-780, PD-799
      IMPLY (targetProt, ! cdsExist == cdss. empty ());  
      const string method (getMethod ());
      const string na ("NA");
      const string proteinName (refExactlyMatched () || ! gi ? product : nvl (getFam () -> familyName, na));
      ASSERT (! contains (proteinName, '\t'));
      vector<CDS> cdss_ (cdss);
      if (cdss_. empty ())
        cdss_. push_back (CDS ());
      for (const CDS& cds : cdss_)
      {
        TabDel td;
      //if (targetProt)
          td << targetName;
        if (cdsExist /*! (gffFName. empty ())*/)
          td << (cds. contig. empty () ? targetName : cds. contig)
             << (cds. contig. empty () ? targetStart : cds. start) + 1
             << (cds. contig. empty () ? targetEnd   : cds. stop)
             << (cds. contig. empty () ? (/*refStrand*/ targetStrand ? '+' : '-') : (cds. strand ? '+' : '-'));
        td << (print_fam 
                 ? famId
                 : (method == "ALLELE" ? famId : nvl (getFam () -> genesymbol, na))
              )
           << proteinName + ifS (reportPseudo, ifS (stopCodon, " " + stopCodonS) + ifS (frameShift, " " + frameShiftS))
           << method
           << (targetProt ? targetLen : targetAlign_aa);  
        if (gi)
          td << refLen
             << refCoverage () * 100  
             << (targetProt ? pRefEffectiveLen () : pIdentity ()) * 100  // refIdentity
             << length
             << accessionProt
             << product
           //<< accessionDna
             ;
        else
          td << na 
             << na
             << na
             << na
             << na
             << na;
        // PD-775
  	    if (const Fam* f = getFam () -> getHmmFam ())
          td << f->hmm
             << f->familyName;
        else
        {
          td << na
             << na;
          ASSERT (method != "HMM");
        }
        os << td. str () << endl;
      }
    }
    

  bool allele () const
    { return famId != gene; }
  size_t targetTail (bool upstream) const
    { return targetStrand == upstream ? targetStart : (targetLen - targetEnd); }
  size_t refEffectiveLen () const
    { return partialDna ? refEnd - refStart : refLen; }
  double pRefEffectiveLen () const
    { ASSERT (nident);
      return (double) nident / (double) refEffectiveLen ();
    }
  double pIdentity () const
    { return (double) nident / (double) length; }
  double refCoverage () const
    { return (double) (refEnd - refStart) / (double) refLen; }
  bool refExactlyMatched () const
    { return    refLen   
             && nident == refLen 
             && refLen == length;
	  }
	string getMethod () const
	  { return refExactlyMatched () 
	             ? allele () && (! targetProt || refLen == targetLen)
	               ? "ALLELE"
	               : "EXACT"  // PD-776
	             : gi
	                ? "BLAST"
	                : "HMM"; 
	  }
	  // PD-736
  bool good () const
    { const double delta = 1e-5;  // PAR
      if (! reportPseudo)
      {
        if (stopCodon)
          return false; 
        if (frameShift)
          return false; 
      }
	    if (targetProt)
	    { if (pRefEffectiveLen () < ident_min - delta)  
  	      return false;
  	  }
  	  else
  	  { // PD-1032
  	    if (   pIdentity ()   < ident_min - delta
  	        || refCoverage () < cover_min - delta
  	       )
  	      return false;
  	  }
	    return true;
    }
private:
  bool insideEq (const BlastAlignment &other) const
    { return    targetStrand                     == other. targetStrand
             && targetStart + mismatchTailTarget >= other. targetStart 
             && targetEnd                        <= other. targetEnd + mismatchTailTarget;
    }
    // Requires: same targetName
  bool descendantOf (const BlastAlignment &other) const
    { return    ! other. allele ()
             && getFam () -> descendantOf (other. getFam ());
    }
  bool betterEq (const BlastAlignment &other) const
    // Reflexive
    { if (targetName != other. targetName)
        return false;
      if (targetProt != other. targetProt)  // ??
        return false;
      // PD-727
    /*if (getFam () != other. getFam ())
      { if (descendantOf (other))
          return true;
        if (other. descendantOf (*this))
          return false;
      }*/
      // PD-807
      if (! other. insideEq (*this))
        return false;
      LESS_PART (other, *this, refExactlyMatched ());  // PD-1261
      LESS_PART (other, *this, nident);
      LESS_PART (*this, other, refEffectiveLen ());
      return accessionProt <= other. accessionProt;  // PD-1245
    }
public:
  const Fam* getFam () const
    { const Fam* fam = famId2fam [famId];
      if (! fam)
        fam = famId2fam [gene];
      ASSERT (fam);
      return fam;
    }
  bool better (const BlastAlignment &other) const
    { return betterEq (other) && ! other. betterEq (*this); }
  bool better (const HmmAlignment& other) const
    { ASSERT (other. good ());
      if (targetName != other. sseqid)
        return false;
      return    refExactlyMatched () 
             || getFam () -> getHmmFam () == other. fam;
    }
  bool operator< (const BlastAlignment &other) const
    { LESS_PART (*this, other, targetName);
      LESS_PART (*this, other, targetStart);
      LESS_PART (*this, other, famId);
      LESS_PART (*this, other, gi);
      return false;
    }
//size_t lengthScore () const
  //{ return refLen - (refEnd - refStart); } 
  void setTargetAlign ()
    { targetAlign = targetEnd - targetStart;
      targetAlign_aa = targetAlign;
      if (! targetProt)
      {
        ASSERT (targetAlign % 3 == 0);
        targetAlign_aa = targetAlign / 3;
      }
    }
};




struct ThisApplication : Application
{
  ThisApplication ()
    : Application ("Report VF..FAM.id's matching proteins")
    {
      // Input
      addKey ("fam", "Table FAM");
      const string blastFormat ("qseqid sseqid length nident qstart qend qlen sstart send slen sseq. qseqid format: gi|Protein accession|DNA accession|fusion part|# fusions|FAM.id|FAM.class|resistance mechanism|Product name");
      addKey ("blastp", "blastp output in the format: " + blastFormat);  
      addKey ("blastx", "blastx output in the format: " + blastFormat);  
      addKey ("gff", ".gff assembly file");
      addKey ("hmmdom", "HMM domain alignments");
      addKey ("hmmsearch", "Output of hmmsearch");
      addKey ("ident_min", "Min. identity to the reference protein (0..1)", "0.9");
      addKey ("cover_min", "Min. coverage of the reference protein (0..1)", "0.9");
      // Output
      addKey ("out", "Identifiers of the reported input proteins");
      addFlag ("print_fam", "Print the FAM.id instead of gene symbol"); 
      addFlag ("pseudo", "Indicate pseudo-genes in the protein name as \"" + stopCodonS + "\" or \"" + frameShiftS + "\""); 
      // Testing
      addFlag ("nosame", "Exclude the same reference ptotein from the BLAST output (for testing)"); 
      addFlag ("noblast", "Exclude the BLAST output (for testing)"); 
    }



  void body () const final
  {
    const string famFName     = getArg ("fam");
    const string blastpFName  = getArg ("blastp");
    const string blastxFName  = getArg ("blastx");
    const string gffFName     = getArg ("gff");
    const string hmmDom       = getArg ("hmmdom");
    const string hmmsearch    = getArg ("hmmsearch");  
                 ident_min    = str2<double> (getArg ("ident_min"));  
                 cover_min    = str2<double> (getArg ("cover_min"));  
    const string outFName     = getArg ("out");
                 print_fam    = getFlag ("print_fam");
                 reportPseudo = getFlag ("pseudo");
    const bool nosame         = getFlag ("nosame");
    const bool noblast        = getFlag ("noblast");
    ASSERT (! famFName. empty ());
    ASSERT (hmmsearch. empty () == hmmDom. empty ());
    ASSERT (ident_min >= 0 && ident_min <= 1);
    ASSERT (cover_min >= 0 && cover_min <= 1);
    
    
    cdsExist =    ! blastxFName. empty ()
               || ! gffFName. empty ();

  
    typedef  List<BlastAlignment>  BlastAls; 
    typedef  List<HmmAlignment>  HmmAls; 
  
  
    // Partial target protein sequences ??
    // Fusion proteins, see PD-283 ??
    
    
    //////////////////////////////////// Input /////////////////////////////////////

    map<string/*locusTag*/, Set<CDS> > locusTag2cdss; 
    if (! gffFName. empty ())
    {
      LineInput f (gffFName /*, 100 * 1024, 1*/);
      while (f. nextLine ())
      {
  	    trim (f. line);
  	    replace (f. line, ' ', '_');
  	    if (   f. line. empty () 
  	        || f. line [0] == '#'
  	       )
  	      continue;
  	    istringstream iss (f. line);
        string contig, method, type, dot, strand, n, rest;
        size_t start, stop;
  	    iss >> contig >> method >> type >> start >> stop >> dot >> strand >> n >> rest;
  	    ASSERT (dot == ".");
  	    ASSERT (start <= stop);
  	    ASSERT (start >= 1);
  	    start--;
  	    if (contains (contig, ":"))
  	      findSplit (contig, ':');  // = project_id
  	    if (   type != "CDS"
  	        && type != "gene"
  	       )
  	      continue;
  	      
  	    ASSERT (   strand == "+" 
  	            || strand == "-"
  	           );
  	           
  	    const bool pseudo = contains (rest, "pseudo=true");
  	    if (pseudo && type == "CDS")  // reportPseudo ??
  	      continue;
  
  	    string locusTag;
  	    const string locusTagName (pseudo ? "locus_tag=" : "Name=");
  	    while (! rest. empty ())
  	    {
    	    locusTag = findSplit (rest, ';');
    	    while (trimPrefix (locusTag, "_"));
    	    if (isLeft (locusTag, locusTagName))
    	      break;
    	  }
   	    if (! isLeft (locusTag, locusTagName))
   	      ERROR_MSG ("No locus tag in " + gffFName + ", line " + toString (f. lineNum));    	  
    	  if (contains (locusTag, ":"))
    	  { EXEC_ASSERT (isLeft (findSplit (locusTag, ':'), locusTagName)); }
    	  else
    	    findSplit (locusTag, '='); 
    	  trimPrefix (locusTag, "\"");
    	  trimSuffix (locusTag, "\"");
    	  
  	    locusTag2cdss [locusTag] << CDS (contig, start, stop, strand == "+");
  	  }
    }
  
  
  	Tree fams; 
  	auto root = new Fam (fams);
  	// Pass 1  
    {
      LineInput f (famFName);  
  	  while (f. nextLine ())
  	  {
  	    trim (f. line);
      //cout << f. line << endl; 
  	    const string famId               (findSplit (f. line, '\t'));
  	    /*const string parentFamName =*/  findSplit (f. line, '\t');
  	    const string genesymbol          (findSplit (f. line, '\t'));
  	    const string hmm                 (findSplit (f. line, '\t'));
  	    const double tc1 = str2<double>  (findSplit (f. line, '\t'));
  	    const double tc2 = str2<double>  (findSplit (f. line, '\t'));
  	    const int reportable = str2<int> (findSplit (f. line, '\t'));
  	    ASSERT (   reportable == 0 
  	            || reportable == 1
  	           );
  	    auto fam = new Fam (fams, root, famId, genesymbol, hmm, tc1, tc2, f. line, reportable);
  	    famId2fam [famId] = fam;
  	    if (! fam->hmm. empty ())
  	      hmm2fam [fam->hmm] = fam;
  	  }
  	}
  	// Pass 2
    {
      LineInput f (famFName);  
  	  while (f. nextLine ())
  	  {
  	    trim (f. line);
  	  //cout << f. line << endl;  
  	    Fam* child = const_cast <Fam*> (famId2fam [findSplit (f. line, '\t')]);
  	    ASSERT (child);
  	    const string parentFamId (findSplit (f. line, '\t'));
  	    Fam* parent = nullptr;
  	    if (! parentFamId. empty ())
  	      { EXEC_ASSERT (parent = const_cast <Fam*> (famId2fam [parentFamId])); }
  	    child->setParent (parent);
  	  }
  	}
    { 
      Unverbose unv;
    	if (verbose ())
    	  fams. print (cout);
    }
    
  
  	// BlastAlignment::good()
    BlastAls blastAls;   
    if (! noblast)
    {
      if (! blastpFName. empty ())
      {
        LineInput f (blastpFName);
    	  while (f. nextLine ())
    	  {
    	    { 
    	      Unverbose unv;
    	      if (verbose ())
    	        cout << f. line << endl;  
    	    }
    	    BlastAlignment al (f. line, true);
    	    al. qc ();  
    	    if (nosame && toString (al. gi) == al. targetName)
    	      continue;
    	    if (al. good ())
    	      blastAls << al;
    	    else
      	    { ASSERT (! al. refExactlyMatched ()); }
    	  }
    	}

      if (! blastxFName. empty ())
      {
        LineInput f (blastxFName);
    	  while (f. nextLine ())
    	  {
    	    { 
    	      Unverbose unv;
    	      if (verbose ())
    	        cout << f. line << endl;  
    	    }
    	    BlastAlignment al (f. line, false);
    	    al. qc ();  
    	    if (nosame && toString (al. gi) == al. targetName)
    	      continue;
    	    if (al. good ())
    	      blastAls << al;
    	    else
      	    { ASSERT (! al. refExactlyMatched ()); }
    	  }
    	}
    }
  	if (verbose ())
  	  cout << "# Good Blasts: " << blastAls. size () << endl;
  	
  
    map<HmmAlignment::Pair, HmmAlignment::Domain> domains;  // Best domain
    if (! hmmDom. empty ())
    {
      LineInput f (hmmDom);
  	  while (f. nextLine ())
  	  {
  	    trim (f. line);
  	    if (   f. line. empty () 
  	        || f. line [0] == '#'
  	       )
  	      continue;
  	  //cout << f. line << endl;  
  	    istringstream iss (f. line);
        string  target_name, accession, query_name, query_accession;
        size_t tlen, qlen, n, of, hmm_from, hmm_to, alignment_from, alignment_to, env_from, env_to;
        double eValue, full_score, full_bias, cValue, i_eValue, domain_score, domain_bias, acc;
  	    iss >> target_name >> accession >> tlen >> query_name >> query_accession >> qlen 
  	        >> eValue >> full_score >> full_bias 
  	        >> n >> of >> cValue >> i_eValue >> domain_score >> domain_bias 
  	        >> hmm_from >> hmm_to >> alignment_from >> alignment_to >> env_from >> env_to
  	        >> acc;
  	    ASSERT (accession == "-");
  	    hmm_from--;
  	    alignment_from--;
  	    env_from--;
  	    const Fam* fam = hmm2fam [query_name];
  	    if (! fam)
  	      continue;
  	    const HmmAlignment::Pair p (target_name, fam->id);
  	    const HmmAlignment::Domain domain (domains [p]);
  	    if (domain. score > domain_score)
  	      continue;
  	    domains [p] = HmmAlignment::Domain (domain_score, qlen, hmm_from, hmm_to, tlen, alignment_from, alignment_to);
  	  //cout << p. first << " " << p. second << " " << domain_score << " " << hmm_from << " " << hmm_to << " " << alignment_from << " " << alignment_to << endl;  
  	  }
    }


  	// HmmAlignment::good()
    HmmAls hmmAls;  
  	if (! hmmsearch. empty ())
  	{
      LineInput f (hmmsearch);
  	  while (f. nextLine ())
  	  {
  	    if (verbose ())
  	      cout << f. line << endl;  
  	    if (f. line. empty () || f. line [0] == '#')
  	      continue;
  	    HmmAlignment hmmAl (f. line);
  	    if (hmmAl. good ())
  	      hmmAls << hmmAl;
  	  }
  	}
  
  

    //////////////////////////////////// Processing ////////////////////////////////////
    // Filtering by ::good() has been done above
        
    // Group by targetName and process each targetName separately for speed ??    

    // Pareto-better()  
    BlastAls goodBlastAls; 
	  for (const auto& blastAl : blastAls)
    {
  	  bool found = false;
  	  for (const auto& goodBlastAl : goodBlastAls)
  	    if (goodBlastAl. better (blastAl))
	      {
	        found = true;
	        break;
	      }
	    if (found)
	      continue;
	      
      for (Iter<BlastAls> goodIter (goodBlastAls); goodIter. next ();)
        if (blastAl. better (*goodIter))
          goodIter. erase ();
          
      goodBlastAls << blastAl;	    
    }
  	if (verbose ())
  	{
  	  cout << "# Best Blasts: " << goodBlastAls. size () << endl;
  	  if (! goodBlastAls. empty ())
  	    goodBlastAls. front (). print (cout);
  	}


    // Pareto-better()  
    HmmAls goodHmmAls; 
    FOR (unsigned char, criterion, 2)
    {
      // hmmAls --> goodHmmAls
      goodHmmAls. clear ();
  	  for (const auto& hmmAl : hmmAls)
      {
    	  bool found = false;
    	  for (const auto& goodHmmAl : goodHmmAls)
    	    if (goodHmmAl. better (hmmAl, criterion))
  	      {
  	        found = true;
  	        break;
  	      }
  	    if (found)
  	      continue;
  
        for (Iter<HmmAls> goodIter (goodHmmAls); goodIter. next ();)
          if (hmmAl. better (*goodIter, criterion))
            goodIter. erase ();
            
        goodHmmAls << hmmAl;
      }
      //
      hmmAls = goodHmmAls;
      if (verbose ())
      {
        cout << "Pareto-better HMMs: (Criterion " << (int) criterion << "): " << hmmAls. size () << endl;
        for (const HmmAlignment& al : hmmAls)
        {
          al. print (cout);
          cout << endl;
        }
      }
    }


    // PD-741
  	if (! hmmsearch. empty ())
      for (Iter<BlastAls> iter (goodBlastAls); iter. next ();)
        if (const Fam* fam = iter->getFam () -> getHmmFam ())  
        {
          bool found = false;
      	  for (const auto& hmmAl : goodHmmAls)
            if (   iter->targetName == hmmAl. sseqid
                && fam == hmmAl. fam
               )
            {
              found = true;
              break;
            }
          if (   ! found            // BLAST is wrong
              && ! iter->refExactlyMatched ()
             )  
            iter. erase ();
        }
  	if (verbose ())
  	  cout << "# Best Blasts left: " << goodBlastAls. size () << endl;


    for (Iter<HmmAls> hmmIt (goodHmmAls); hmmIt. next ();)
  	  for (const auto& blastAl : goodBlastAls)
  	    if (blastAl. better (*hmmIt))
	      {
          hmmIt. erase ();
	        break;
	      }


    ////////////////////////////////////// Output ///////////////////////////////////////

    // goodHmmAls --> goodBlastAls
  	for (const auto& hmmAl : goodHmmAls)
  	{
  	  BlastAlignment al (hmmAl);
  	//cout << al. targetName << " " << al. gene << endl;  
  	  const HmmAlignment::Domain domain = domains [HmmAlignment::Pair (al. targetName, al. gene)];
  	  if (! domain. hmmLen)  
  	    continue;  // domain does not exist
  	/*al. refLen      = domain. hmmLen;
  	  al. refStart    = domain. hmmStart;
  	  al. refEnd      = domain. hmmStop; */
  	  al. targetLen   = domain. seqLen;
  	  al. targetStart = domain. seqStart;
  	  al. targetEnd   = domain. seqStop;
  	  al. setTargetAlign ();
  	  ASSERT (! al. refExactlyMatched ());
  	  al. qc ();
  	  goodBlastAls << al;
  	}

  
    if (! gffFName. empty ())
    	for (auto& al : goodBlastAls)
    	  if (al. targetProt)
      	{
      	  string s (al. targetName);
      	  trimSuffix (s, "|");
      	  const string locusTag (rfindSplit (s, '|'));
      	  const Set<CDS>& cdss = locusTag2cdss [locusTag];
      	  if (cdss. empty ())
      	    ERROR_MSG ("Locus tag \"" + locusTag + "\" is misssing in .gff file. Protein name: " + al. targetName);
      	  ASSERT (al. cdss. empty ());
      	  insertAll (al. cdss, cdss);
      	  al. qc ();
      	}
    
    
    goodBlastAls. sort ();
  
  
    // PD-283, PD-780
    // Cf. BlastAlignment::saveText()
    TabDel td;
    td << "Target identifier";  // targetName
    if (cdsExist /*! gffFName. empty ()*/)  // Cf. BlastAlignment::saveText()
      // Contig
      td << "Contig id"
         << "Start"  // targetStart
         << "Stop"  // targetEnd
         << "Strand";  // targetStrand
    td << (print_fam ? "FAM.id" : "Gene symbol")
       << "Protein name"
       << "Method"
       << "Target length" 
       //
       << "Reference protein length"    // refLen
       << "% Coverage of reference protein"  // queryCoverage
       << "% Identity to reference protein"  
       << "Alignment length"  // length
       << "Accession of closest protein"      // accessionProt
       << "Name of closest protein"
       //
       << "HMM id"
       << "HMM description"
       ;
    {
      OFStream ofs;
      if (! outFName. empty ())
        ofs. open (string (), outFName, string ());
      cout << td. str () << endl;
    	for (const auto& blastAl : goodBlastAls)
    	  if (blastAl. getFam () -> reportable)
      	{
      	  blastAl. qc ();
      	  blastAl. print (cout);
          if (! outFName. empty ())
            ofs << blastAl. targetName << endl;
      	}
    }
  }
};



}  // namespace



int main (int argc, 
          const char* argv[])
{
  ThisApplication app;
  return app. run (argc, argv);  
}


