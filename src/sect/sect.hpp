#if !defined(DEF_SECT_H)
#define DEF_SECT_H

#include <iostream>
#include <string.h>
#include <stdint.h>
#include <vector>

#include <jellyfish/counter.hpp>
#include <jellyfish/thread_exec.hpp>

using std::vector;
using std::string;

template<typename hash_t>
class Sect : public thread_exec
{
    const hash_t            *hash;		// Jellyfish hash
    const vector<string>    *names;	    // Names of fasta sequences (in same order as seqs)
    const vector<string>    *seqs;	    // Sequences in fasta sequences (in same order as names)
    const uint_t            kmer;		// Kmer size to use
    const uint_t            threads;	// Number of threads to use
    const size_t            bucket_size, remaining;	// Chunking vars
    vector<vector<uint_t>*> *counts;    // Kmer counts for each kmer window in sequence (in same order as seqs and names; built by this class)
    vector<float>           *coverages; // Overall coverage calculated for each sequence from the kmer windows.

public:
    Sect(const hash_t *_hash, const vector<string> *_names, const vector<string> *_seqs,
         uint_t _kmer, uint_t _threads) :
        hash(_hash), names(_names), seqs(_seqs),
        kmer(_kmer), threads(_threads),
        bucket_size(seqs->size() / threads),
        remaining(seqs->size() % (bucket_size < 1 ? 1 : threads))
    {
        counts = new vector<vector<uint_t>*>(seqs->size());
        coverages = new vector<float>(seqs->size());
    }

    ~Sect()
    {
        if(counts)
        {
            for(uint_t i = 0; i < counts->size(); i++)
            {
                delete (*counts)[i];
            }
            delete counts;
        }

        if (coverages)
            delete coverages;
    }


    void do_it()
    {
        exec_join(threads);
    }

    void start(int th_id)
    {
        // Check to see if we have useful work to do for this thread, return if not
        if (bucket_size < 1 && th_id >= seqs->size())
        {
            return;
        }

        //processInBlocks(th_id);
        processInterlaced(th_id);

    }


    void printVars()
    {
        std::cerr << "Sequences to process: " << seqs->size() << "\n";
        std::cerr << "Kmer: " << kmer << "\n";
        std::cerr << "Threads: " << threads << "\n";
        std::cerr << "Bucket size: " << bucket_size << "\n";
        std::cerr << "Remaining: " << remaining << "\n";
    }


    void printCounts(std::ostream &out)
    {
        for(int i = 0; i < names->size(); i++)
        {
            out << ">" << (*names)[i].c_str() << "\n";

            vector<uint64_t>* seqCounts = (*counts)[i];

            if (seqCounts != NULL)
            {
                for(uint_t j = 0; j < seqCounts->size(); j++)
                {
                    out << " " << (*seqCounts)[j];
                }
                out << "\n";
            }
            else
            {
                out << " 0\n";
            }
        }
    }

    void printCoverages(std::ostream &out)
    {
        for(int i = 0; i < names->size(); i++)
        {
            out << (*coverages)[i] << "\n";
        }
    }


private:

    // This method won't be optimal in most cases... Fasta files are normally sorted by length (largest first)
    // So first thread will be asked to do more work than the rest
    void processInBlocks(uint_t th_id)
    {
        size_t start = bucket_size < 1 ? th_id : th_id * bucket_size;
        size_t end = bucket_size < 1 ? th_id : start + bucket_size - 1;
        for(size_t i = start; i <= end; i++)
        {
            processSeq(i);
        }

        // Process a remainder if required
        if (th_id < remaining)
        {
            size_t rem_idx = (threads * bucket_size) + th_id;
            processSeq(rem_idx);
        }
    }

    // This method is probably makes more efficient use of multiple cores on a length sorted fasta file
    void processInterlaced(uint_t th_id)
    {
        size_t start = th_id;
        size_t end = seqs->size();
        for(size_t i = start; i < end; i+=threads)
        {
            processSeq(i);
        }
    }

    void processSeq(const size_t index)
    {
        string seq = (*seqs)[index];
        uint_t nbCounts = seq.length() - kmer + 1;

        if (seq.length() < kmer)
        {
            std::cerr << (*names)[index].c_str() << ": " << seq << "  is too short to compute coverage.  Setting sequence coverage to 0.\n";
            return;
        }

        std::cerr << "Seq: " << seq << "; Counts to create: " << nbCounts << "\n";

        vector<uint_t>* seqCounts = new vector<uint_t>(nbCounts);

        uint64_t sum = 0;

        for(uint_t i = 0; i < nbCounts; i++)
        {

            string merstr = seq.substr(i, kmer);

            // Jellyfish compacted hash does not support Ns so if we find one set this mer count to 0
            if (merstr.find("N") != string::npos)
            {
                (*seqCounts)[i] = 0;
            }
            else
            {
                const char* mer = merstr.c_str();
                uint_t count = (*hash)[mer];
                sum += count;

                (*seqCounts)[i] = count;
            }
        }

        (*counts)[index] = seqCounts;

        // Assumes simple mean calculation for sequence coverage for now... plug in Bernardo's method later.
        (*coverages)[index] = (float)sum / (float)nbCounts;
    }

};

#endif //DEF_SECT_H
