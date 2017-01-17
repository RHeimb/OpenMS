#include <OpenMS/ANALYSIS/ID/BuildIndexing.h>
#include <OpenMS/KERNEL/StandardTypes.h>
#include <OpenMS/CHEMISTRY/EnzymaticDigestion.h>
//#include <OpenMS/DATASTRUCTURES/SeqanIncludeWrapper.h>
#include <OpenMS/METADATA/ProteinIdentification.h>
#include <OpenMS/SYSTEM/StopWatch.h>
#include <OpenMS/METADATA/PeptideEvidence.h>
#include <OpenMS/CHEMISTRY/EnzymesDB.h>
#include <OpenMS/DATASTRUCTURES/ListUtils.h>
#include <seqan2/seq_io.h>
#include <seqan2/index.h>
#include <seqan2/seeds.h>
#include <seqan2/arg_parse.h>
#include <seqan2/bam_io.h>
#include <seqan2/find.h>
#include <seqan2/basic.h>
#include <seqan2/stream.h>
#include <fstream>
#include <iostream>
#include <algorithm>

using namespace OpenMS;
using namespace std;

/* */
struct FMind {
    FMind() { }
};

/* */
struct SAind {
    SAind() { }
};

//namespace seqan2 {
//    template<>
//    struct SAValue<StringSet<Peptide> > {
//    // anzahl strings - laenge strings
//        typedef Pair<unsigned, unsigned> Type;
//    };
//}

BuildIndexing::BuildIndexing() :
        DefaultParamHandler("BuildIndexing") {
    defaults_.setValue("suffix_array", "false", "Using the Suffix Array as index");
    defaults_.setValidStrings("suffix_array", ListUtils::create<String>("true,false"));

    defaults_.setValue("FM_index", "false", "Using the FM-Index as index");
    defaults_.setValidStrings("FM_index", ListUtils::create<String>("true,false"));

     defaults_.setValue("IL_equivalent", "false",
                       "Treat the isobaric amino acids isoleucine ('I') and leucine ('L') as equivalent (indistinguishable)");
    defaults_.setValidStrings("IL_equivalent", ListUtils::create<String>("true,false"));
//
    defaults_.setValue("log", "", "Name of log file (created only when specified)");
    defaults_.setValue("debug", 0, "Sets the debug level");

    defaultsToParam_();
}

BuildIndexing::~BuildIndexing() {

}

BuildIndexing::ExitCodes BuildIndexing::saveOutput_(Map<String, Size> &acc_to_prot,
                                                    Map<String, Size> &acc_to_AAAprot,
                                                    std::vector<FASTAFile::FASTAEntry>& proteins,
                                                    std::vector<FASTAFile::FASTAEntry>& proteinsAAA,
                                                    String &out){
    // save them on disk
    // jede accession nummer wird die Position in dem Suffix-Array gespeichert
    // Eintrag: CAQ30518;1383
    // accession Nummern sind alphabetisch geordet
    std::ofstream acc_to_prot_out((out + "_acc_to_prot").c_str());
    for (map<OpenMS::String, unsigned long>::iterator i = acc_to_prot.begin(); i != acc_to_prot.end(); i++){
        acc_to_prot_out << (*i).first;
        acc_to_prot_out << ";";
        acc_to_prot_out << (*i).second;
        acc_to_prot_out << "\n";
    }
    acc_to_prot_out.close();

    std::ofstream acc_to_AAAprot_out((out + "_AAA_acc_to_prot").c_str());
    for (map<OpenMS::String, unsigned long>::iterator i = acc_to_AAAprot.begin(); i != acc_to_AAAprot.end(); i++){
        acc_to_AAAprot_out << (*i).first;
        acc_to_AAAprot_out << ";";
        acc_to_AAAprot_out << (*i).second;
        acc_to_AAAprot_out << "\n";
    }
    acc_to_AAAprot_out.close();

    // speichert quasi die Fasta erneut.
    std::ofstream proteins_out((out + "_proteins").c_str());
    for (vector<OpenMS::FASTAFile::FASTAEntry>::iterator i = proteins.begin(); i != proteins.end(); i++){
        proteins_out << (*i).identifier;
        proteins_out << ";";
        proteins_out << (*i).sequence;
        proteins_out << ";";
        proteins_out << (*i).description;
        proteins_out << "\n";
    }
    proteins_out.close();

    std::ofstream proteinsAAA_out((out + "_AAA_proteins").c_str());
    for (vector<OpenMS::FASTAFile::FASTAEntry>::iterator i = proteinsAAA.begin(); i != proteinsAAA.end(); i++){
        proteinsAAA_out << (*i).identifier;
        proteinsAAA_out << ";";
        proteinsAAA_out << (*i).sequence;
        proteinsAAA_out << ";";
        proteinsAAA_out << (*i).description;
        proteinsAAA_out << "\n";
    }
    proteinsAAA_out.close();

    return CHECKPOINT_OK;

}

void BuildIndexing::writeLog_(const String &text) const {
    LOG_INFO << text << endl;
    if (!log_file_.empty()) {
        log_ << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString() << ": " << text << endl;
    }
}

void BuildIndexing::writeDebug_(const String &text, const Size min_level) const {
    if (debug_ >= min_level) {
        writeLog_(text);
    }
}

BuildIndexing::ExitCodes BuildIndexing::run(std::vector<FASTAFile::FASTAEntry>& proteins, String &out) {
    //-------------------------------------------------------------
    // parsing parameters
    //-------------------------------------------------------------
    if (!log_file_.empty()) {
        log_.open(log_file_.c_str());
    }


    //-------------------------------------------------------------
    // calculations
    //-------------------------------------------------------------

    ExitCodes erg = checkUserInput_(proteins);
    if (erg != CHECKPOINT_OK){
        return erg;
    }

    /**
       BUILD Protein DB
    */
    writeLog_(String("Building protein database..."));
    std::vector<FASTAFile::FASTAEntry> proteinsAAA;
    Map<String, Size> acc_to_prot; // build map: accessions to FASTA protein index
    Map<String, Size> acc_to_AAAprot;
    seqan2::StringSet<seqan2::Peptide> prot_DB;
    seqan2::StringSet<seqan2::Peptide> prot_DB_AAA;
    vector<String> duplicate_accessions;
    // check for duplicated accessions & build prot DB
    if (buildProtDB_(proteins, proteinsAAA, acc_to_prot, prot_DB, acc_to_AAAprot, prot_DB_AAA, duplicate_accessions) != CHECKPOINT_OK ) {
        return DATABASE_CONTAINS_MULTIPLES;
    }

    /**
    BUILD Index & Save
    */
    writeLog_(String("Building protein database index..."));
    if (suffix_array_){
        if (build_index_(prot_DB,prot_DB_AAA,out,SAind()) != CHECKPOINT_OK){
            return OUTPUT_ERROR;
        }
    }else {
        if (build_index_(prot_DB,prot_DB_AAA,out,FMind()) != CHECKPOINT_OK){
            return OUTPUT_ERROR;
        }
    }

    writeLog_(String("saving protein database index..."));
    saveOutput_(acc_to_prot,acc_to_AAAprot, proteins, proteinsAAA, out);


    return EXECUTION_OK;
}

BuildIndexing::ExitCodes BuildIndexing::build_index_(seqan2::StringSet<seqan2::Peptide> &prot_DB,
                                                     seqan2::StringSet<seqan2::Peptide> &prot_DB_AAA,
                                                     String out,
                                                     SAind /**/){
    if (!seqan2::empty(prot_DB_AAA)){
        seqan2::Index<seqan2::StringSet<seqan2::Peptide>, seqan2::IndexSa<> > indexAAA(prot_DB_AAA);
        seqan2::indexRequire(indexAAA, seqan2::FibreSA());
        if (!seqan2::save(indexAAA, (out + "_AAA").c_str() )) {
            LOG_ERROR << "Error: Could not save output to disk. Check for correct name, available space and privileges..." << std::endl;
            return OUTPUT_ERROR;
        }
    }
    if (!seqan2::empty(prot_DB)){
        seqan2::Index<seqan2::StringSet<seqan2::Peptide>, seqan2::IndexSa<> > index(prot_DB);
        seqan2::indexRequire(index, seqan2::FibreSA());
        if (!seqan2::save(index, out.c_str() )) {
            LOG_ERROR << "Error: Could not save output to disk. Check for correct name, available space and privileges..." << std::endl;
            return OUTPUT_ERROR;
        }
    }
    return CHECKPOINT_OK;
}

BuildIndexing::ExitCodes BuildIndexing::build_index_(seqan2::StringSet<seqan2::Peptide> &prot_DB,
                                                     seqan2::StringSet<seqan2::Peptide> &prot_DB_AAA,
                                                     String out,
                                                     FMind /**/){
    //typedef seqan2::FastFMIndexConfig<void,std::uint32_t,2,1> TFastFMConfig;
    if (!seqan2::empty(prot_DB_AAA)){
        //seqan2::Index<seqan2::StringSet<seqan2::Peptide>, seqan2::FMIndex<void, TFastFMConfig> > indexAAA(prot_DB_AAA);
        seqan2::Index<seqan2::StringSet<seqan2::Peptide>, seqan2::FMIndex<> > indexAAA(prot_DB_AAA);
        seqan2::indexRequire(indexAAA, seqan2::FibreSALF());
        if (!seqan2::save(indexAAA, (out + "_AAA").c_str() )) {
            LOG_ERROR << "Error: Could not save output to disk. Check for correct name, available space and privileges..." << std::endl;
            return OUTPUT_ERROR;
        }
    }
    if (!seqan2::empty(prot_DB)){
        //seqan2::Index<seqan2::StringSet<seqan2::Peptide>, seqan2::FMIndex<void, TFastFMConfig> > index(prot_DB);
        seqan2::Index<seqan2::StringSet<seqan2::Peptide>, seqan2::FMIndex<> > index(prot_DB);
        seqan2::indexRequire(index, seqan2::FibreSALF());
        if (!seqan2::save(index, out.c_str() )) {
            LOG_ERROR << "Error: Could not save output to disk. Check for correct name, available space and privileges..." << std::endl;
            return OUTPUT_ERROR;
        }
    }
    return CHECKPOINT_OK;
}

BuildIndexing::ExitCodes BuildIndexing::check_duplicate_(std::vector<FASTAFile::FASTAEntry>& proteins,
                                                         String seq,
                                                         std::vector<String> &duplicate_accessions,
                                                         Map<String, Size> &acc_to_prot,
                                                         String &acc,
                                                         seqan2::StringSet<seqan2::Peptide>  &prot_DB,
                                                         Size protIndex){
    if (acc_to_prot.has(acc)) {
        duplicate_accessions.push_back(acc);
        // check if sequence is identical
        const seqan2::Peptide &tmp_prot = prot_DB[acc_to_prot[acc]];
        if (String(begin(tmp_prot), end(tmp_prot)) != seq) {
            LOG_ERROR << "Fatal error: Protein identifier '" << acc <<
                      "' found multiple times with different sequences" << (IL_equivalent_ ? " (I/L substituted)" : "") <<
                      ":\n"
                      << tmp_prot << "\nvs.\n" << seq << "\nPlease fix the database and build index again." <<
                      std::endl;
            return BuildIndexing::DATABASE_CONTAINS_MULTIPLES;
        }
        // Remove duplicate entry from 'proteins', since 'prot_DB' and 'proteins' need to correspond 1:1 (later indexing depends on it)
        // The other option would be to allow two identical entries, but later on, only the last one will be reported (making the first protein an orphan; implementation details below)
        // Thus, the only safe option is to remove the duplicate from 'proteins' and not to add it to 'prot_DB'
        proteins.erase(proteins.begin() + protIndex);
        return BuildIndexing::CHECKPOINT_DONE;
    }
    return BuildIndexing::CHECKPOINT_OK;
}

bool BuildIndexing::has_aaa_(String seq){
    for (Size j = 0; j < seq.length(); ++j) {
        if ((seq[j] == 'B') || (seq[j] == 'X') ||
            (seq[j] == 'Z')) {
            return true;
        }
    }
    return false;
}

void BuildIndexing::updateMembers_() {
    suffix_array_ = param_.getValue("suffix_array").toBool();
    FM_index_ = param_.getValue("FM_index").toBool();
    log_file_ = param_.getValue("log");
    debug_ = static_cast<Size>(param_.getValue("debug")) > 0;
}

BuildIndexing::ExitCodes BuildIndexing::checkUserInput_(std::vector<FASTAFile::FASTAEntry> &proteins) {
    if (seqan2::empty(proteins)) // we do not allow an empty database
    {
        LOG_ERROR << "Error: An empty database was provided. Mapping makes no sense. Aborting..." << std::endl;
        return DATABASE_EMPTY;
    }
    if (!suffix_array_ && !FM_index_){
        LOG_ERROR <<
                  "Fatal error: Specify a format to build index.\n"
                  << "Please adapt your settings." << endl;
        return ILLEGAL_PARAMETERS;
    }
    if (suffix_array_ && FM_index_){
        LOG_ERROR <<
                  "Fatal error: Specify only one format to build index.\n"
                  << "Please adapt your settings." << endl;
        return ILLEGAL_PARAMETERS;
    }
    return CHECKPOINT_OK;
}

BuildIndexing::ExitCodes BuildIndexing::appendWrapper_(std::vector<FASTAFile::FASTAEntry>& proteins,
                                                      String &seq,
                                                      std::vector<String> &duplicate_accessions,
                                                      Map<String, Size> &acc_to_prot,
                                                      String &acc,
                                                      seqan2::StringSet<seqan2::Peptide> &db,
                                                      Size &i){
    // check for duplicate proteins in db
    ExitCodes erg = check_duplicate_(proteins, seq, duplicate_accessions, acc_to_prot,acc,db,i);
    if (erg != BuildIndexing::CHECKPOINT_OK) {
        if (erg != BuildIndexing::CHECKPOINT_DONE){
            // case database contained multiples
            return erg;
        }
        // case where duplicate entry has been removed!
        i--;
    } else {
        // add protein to db
        seqan2::appendValue(db, seq.c_str());
        acc_to_prot[acc] = i;
    }
    return BuildIndexing::CHECKPOINT_OK;
}

BuildIndexing::ExitCodes BuildIndexing::buildProtDB_(std::vector<FASTAFile::FASTAEntry>& proteins,
                                                     std::vector<FASTAFile::FASTAEntry>& proteinsAAA,
                                                     Map<String, Size> &acc_to_prot,
                                                     seqan2::StringSet<seqan2::Peptide> &prot_DB,
                                                     Map<String, Size> &acc_to_AAAprot,
                                                     seqan2::StringSet<seqan2::Peptide> &prot_DB_AAA,
                                                     std::vector<String> &duplicate_accessions){
    Size j = 0;
    for (Size i = 0; i != proteins.size(); ++i) {
        String seq = proteins[i].sequence.remove('*');
        if (IL_equivalent_) // convert  L to I; warning: do not use 'J', since Seqan does not know about it and will convert 'J' to 'X'
        {
            seq.substitute('L', 'I');
        }
        String acc = proteins[i].identifier;

        // check if the protein contains ambiguous amino acids:
        if (has_aaa_(seq)){
            // case AAA
            proteinsAAA.push_back(proteins[i]);
            proteins.erase(proteins.begin() + i);
            i--;
            if (appendWrapper_(proteinsAAA,seq,duplicate_accessions,acc_to_AAAprot,acc,prot_DB_AAA,j)!=BuildIndexing::CHECKPOINT_OK){
                return BuildIndexing::DATABASE_CONTAINS_MULTIPLES;
            };
            j++;
        }else{
            // case normal
            if (BuildIndexing::appendWrapper_(proteins,seq,duplicate_accessions,acc_to_prot,acc,prot_DB,i)!=BuildIndexing::CHECKPOINT_OK){
                return BuildIndexing::DATABASE_CONTAINS_MULTIPLES;
            };
        }
    }
    return BuildIndexing::CHECKPOINT_OK;
}
