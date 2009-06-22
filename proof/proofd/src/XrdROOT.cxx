// @(#)root/proofd:$Id$
// Author: Gerardo Ganis  June 2007

/*************************************************************************
 * Copyright (C) 1995-2005, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// XrdROOT                                                              //
//                                                                      //
// Authors: G. Ganis, CERN, 2007                                        //
//                                                                      //
// Class describing a ROOT version                                      //
//                                                                      //
//////////////////////////////////////////////////////////////////////////
#include "RConfigure.h"

#include "XrdProofdPlatform.h"

#include "XrdROOT.h"
#include "XrdProofdManager.h"
#include "XrdProofdProtocol.h"
#include "XrdProofdProofServMgr.h"
#include "XrdOuc/XrdOucStream.hh"
#include "XrdSys/XrdSysPriv.hh"
#include "XrdSys/XrdSysLogger.hh"

// Tracing
#include "XrdProofdTrace.h"

//__________________________________________________________________________
XrdROOT::XrdROOT(const char *dir, const char *tag, const char *bindir,
                 const char *incdir, const char *libdir, const char *datadir)
{
   // Constructor: validates 'dir', gets the version and defines the tag.
   XPDLOC(SMGR, "XrdROOT")

   fStatus = -1;
   fSrvProtVers = -1;

   // 'dir' must make sense
   if (!dir || strlen(dir) <= 0)
      return;
   if (tag && strlen(tag) > 0) {
      fExport = tag;
      fExport += " "; fExport += dir;
   } else
      fExport += dir;
   // ... and exist
   if (CheckDir(dir) != 0) return;
   fDir = dir;

   // Include dir
   fIncDir = incdir;
   if (!incdir || strlen(incdir) <= 0) {
      fIncDir = fDir;
      fIncDir += "/include";
   }
   if (CheckDir(fIncDir.c_str()) != 0) return;

   // Get the version
   XrdOucString version;
   if (GetROOTVersion(version) == -1) {
      TRACE(XERR, "unable to extract ROOT version from path "<<fIncDir);
      return;
   }

   // Default tag is the version
   fTag = (!tag || strlen(tag) <= 0) ? version : tag;

   // Lib dir
   fLibDir = libdir;
   if (!libdir || strlen(libdir) <= 0) {
      fLibDir = fDir;
      fLibDir += "/lib";
   }
   if (CheckDir(fLibDir.c_str()) != 0) return;

   // Bin dir
   fBinDir = bindir;
   if (!bindir || strlen(bindir) <= 0) {
      fBinDir = fDir;
      fBinDir += "/bin";
   }
   if (CheckDir(fBinDir.c_str()) != 0) return;

   // Data dir
   fDataDir = datadir;
   if (!datadir || strlen(datadir) <= 0) {
      fDataDir = fDir;
   }
   if (CheckDir(fDataDir.c_str()) != 0) return;

   // The application to be run
   fPrgmSrv = fBinDir;
   fPrgmSrv += "/proofserv";

   // Export string
   fExport = fTag;
   fExport += " "; fExport += version;
   fExport += " "; fExport += dir;

   // First step OK
   fStatus = 0;
}

//__________________________________________________________________________
int XrdROOT::CheckDir(const char *dir)
{
   // Check if 'dir' exists
   // Return 0 on succes, -1 on failure
   XPDLOC(SMGR, "CheckDir")

   if (dir && strlen(dir) > 0) {
      // The path should exist and be statable
      struct stat st;
      if (stat(dir, &st) == -1) {
         TRACE(XERR, "unable to stat path "<<dir);
         return -1;
      }
      // ... and be a directory
      if (!S_ISDIR(st.st_mode)) {
         TRACE(XERR, "path "<<dir<<" is not a directory");
         return -1;
      }
      // Ok
      return 0;
   }
   TRACE(XERR, "path is undefined");
   return -1;
}

//__________________________________________________________________________
void XrdROOT::SetValid(kXR_int16 vers)
{
   // Set valid, save protocol and finalize the export string

   fStatus = 1;

   if (vers > 0) {
      // Cleanup export, if needed
      if (fSrvProtVers > 0) {
         XrdOucString vs(" ");
         vs += fSrvProtVers;
         fExport.replace(vs,XrdOucString(""));
      }
      fSrvProtVers = vers;

      // Finalize export string
      fExport += " ";
      fExport += (int)fSrvProtVers;
   }
}

//__________________________________________________________________________
int XrdROOT::GetROOTVersion(XrdOucString &version)
{
   // Get ROOT version associated with 'dir'.
   XPDLOC(SMGR, "GetROOTVersion")

   int rc = -1;

   XrdOucString versfile = fIncDir;
   versfile += "/RVersion.h";

   // Open file
   FILE *fv = fopen(versfile.c_str(), "r");
   if (!fv) {
      TRACE(XERR, "unable to open "<<versfile);
      return rc;
   }

   // Read the file
   char line[1024];
   while (fgets(line, sizeof(line), fv)) {
      char *pv = (char *) strstr(line, "ROOT_RELEASE");
      if (pv) {
         if (line[strlen(line)-1] == '\n')
            line[strlen(line)-1] = 0;
         pv += strlen("ROOT_RELEASE") + 1;
         version = pv;
         version.replace("\"","");
         rc = 0;
         break;
      }
   }

   // Close the file
   fclose(fv);

   // Done
   return rc;
}

//
// Manager

//______________________________________________________________________________
XrdROOTMgr::XrdROOTMgr(XrdProofdManager *mgr,
                       XrdProtocol_Config *pi, XrdSysError *e)
          : XrdProofdConfig(pi->ConfigFN, e)
{
   // Constructor
   fMgr = mgr;
   fSched = pi->Sched;
   fLogger = pi->eDest->logger();
   fROOT.clear();

   // Configuration directives
   RegisterDirectives();
}

//__________________________________________________________________________
void XrdROOTMgr::SetLogDir(const char *dir)
{
   // Set the log dir
   XPDLOC(SMGR, "ROOTMgr::SetLogDir")

   if (fMgr && dir && strlen(dir)) {
      // MAke sure that the directory to store logs from validation exists
      XPDFORM(fLogDir, "%s/rootsysvalidation", dir);
      XrdProofUI ui;
      XrdProofdAux::GetUserInfo(fMgr->EffectiveUser(), ui);
      if (XrdProofdAux::AssertDir(fLogDir.c_str(), ui, fMgr->ChangeOwn()) != 0) {
         XPDERR("unable to assert the rootsys log validation path: "<<fLogDir);
         fLogDir = "";
      } else {
         TRACE(ALL,"rootsys log validation path: "<<fLogDir);
      }
   }
}

//__________________________________________________________________________
int XrdROOTMgr::Config(bool rcf)
{
   // Run configuration and parse the entered config directives.
   // Return 0 on success, -1 on error
   XPDLOC(SMGR, "ROOTMgr::Config")

   // Run first the configurator
   if (XrdProofdConfig::Config(rcf) != 0) {
      TRACE(XERR, "problems parsing file ");
      return -1;
   }

   XrdOucString msg;
   msg = (rcf) ? "re-configuring" : "configuring";
   TRACE(ALL, msg);

   // ROOT dirs
   if (rcf) {
      // Remove parked ROOT sys entries
      std::list<XrdROOT *>::iterator tri;
      if (fROOT.size() > 0) {
         for (tri = fROOT.begin(); tri != fROOT.end();) {
            if ((*tri)->IsParked()) {
               delete (*tri);
               tri = fROOT.erase(tri);
            } else {
               tri++;
            }
         }
      }
   } else {
      // Check the ROOT dirs
      if (fROOT.size() <= 0) {
#ifdef R__HAVE_CONFIG
         XrdOucString dir(ROOTPREFIX), bd(ROOTBINDIR), ld(ROOTLIBDIR),
                      id(ROOTINCDIR), dd(ROOTDATADIR);
#else
         XrdOucString dir(getenv("ROOTSYS")), bd, ld, id, dd;
#endif
         // None defined: use ROOTSYS as default, if any; otherwise we fail
         if (dir.length() > 0) {
            XrdROOT *rootc = new XrdROOT(dir.c_str(), "",
                                         bd.c_str(), id.c_str(), ld.c_str(), dd.c_str());
            XPDFORM(msg, "ROOT dist: '%s'", rootc->Export());
            if (Validate(rootc, fSched) == 0) {
               msg += " validated";
               fROOT.push_back(rootc);
               TRACE(ALL, msg);
            } else {
               msg += " could not be validated";
               TRACE(XERR, msg);
            }
         }
         if (fROOT.size() <= 0) {
            TRACE(XERR, "no ROOT dir defined; ROOTSYS location missing - unloading");
            return -1;
         }
      }
   }

   // Done
   return 0;
}

//__________________________________________________________________________
void XrdROOTMgr::RegisterDirectives()
{
   // Register directives for configuration

   Register("rootsys", new XrdProofdDirective("rootsys", this, &DoDirectiveClass));
}

//______________________________________________________________________________
int XrdROOTMgr::DoDirective(XrdProofdDirective *d,
                            char *val, XrdOucStream *cfg, bool rcf)
{
   // Update the priorities of the active sessions.
   XPDLOC(SMGR, "ROOTMgr::DoDirective")

   if (!d)
      // undefined inputs
      return -1;

   if (d->fName == "rootsys") {
      return DoDirectiveRootSys(val, cfg, rcf);
   }
   TRACE(XERR, "unknown directive: "<<d->fName);
   return -1;
}

//______________________________________________________________________________
int XrdROOTMgr::DoDirectiveRootSys(char *val, XrdOucStream *cfg, bool)
{
   // Process 'rootsys' directive
   XPDLOC(SMGR, "ROOTMgr::DoDirectiveRootSys")

   if (!val || !cfg)
      // undefined inputs
      return -1;

   // Two tokens may be meaningful
   XrdOucString dir = val;
   val = cfg->GetWord();
   XrdOucString tag = val;
   bool ok = 1;
   if (tag == "if") {
      tag = "";
      // Conditional
      cfg->RetToken();
      ok = (XrdProofdAux::CheckIf(cfg, fMgr->Host()) > 0) ? 1 : 0;
   }
   if (ok) {
      // Check for additional info in the form: bindir incdir libdir datadir
      XrdOucString a[4];
      int i = 0;
      while ((val = cfg->GetWord())) { a[i++] = val; }
      XrdROOT *rootc = new XrdROOT(dir.c_str(), tag.c_str(), a[0].c_str(),
                                   a[1].c_str(), a[2].c_str(), a[3].c_str());
      // Check if already validated
      std::list<XrdROOT *>::iterator ori;
      for (ori = fROOT.begin(); ori != fROOT.end(); ori++) {
         if ((*ori)->Match(rootc->Dir(), rootc->Tag())) {
            if ((*ori)->IsParked()) {
               (*ori)->SetValid();
               SafeDelete(rootc);
               break;
            }
         }
      }
      // If not, try validation
      if (rootc) {
         if (Validate(rootc, fSched) == 0) {
            TRACE(REQ, "validation OK for: "<<rootc->Export());
            // Add to the list
            fROOT.push_back(rootc);
         } else {
            TRACE(XERR, "could not validate "<<rootc->Export());
            SafeDelete(rootc);
         }
      }
   }
   return 0;
}

//__________________________________________________________________________
int XrdROOTMgr::Validate(XrdROOT *r, XrdScheduler *sched)
{
   // Start a trial server application to test forking and get the version
   // of the protocol run by the PROOF server.
   // Return 0 if everything goes well, -1 in case of any error.
   XPDLOC(SMGR, "ROOTMgr::Validate")

   TRACE(REQ, "forking test and protocol retrieval");

   if (r->IsInvalid()) {
      // Cannot be validated
      TRACE(XERR, "invalid instance - cannot be validated");
      return -1;
   }

   // Make sure the application path has been defined
   if (!r->PrgmSrv() || strlen(r->PrgmSrv()) <= 0) {
      TRACE(XERR, "path to PROOF server application undefined - exit");
      return -1;
   }

   // Make sure the scheduler is defined
   if (!sched) {
      TRACE(XERR, "scheduler undefined - exit");
      return -1;
   }

   // Pipe to communicate the protocol number
   int fp[2];
   if (pipe(fp) != 0) {
      TRACE(XERR, "PROOT protocol number communication");
      return -1;
   }

   // Debug flag
   bool debug = 0;
   if (TRACING(DBG)) debug = 1;

   // Log the attemp into this file
   XrdOucString logfile, rootrc;
   if (fLogDir.length() > 0) {
      XrdOucString tag(r->Tag());
      tag.replace("/","-");
      XPDFORM(logfile, "%s/root.%s.log", fLogDir.c_str(), tag.c_str());
      if (debug) {
         XPDFORM(rootrc, "%s/root.%s.rootrc", fLogDir.c_str(), tag.c_str());
      }
   }

   // Fork a test agent process to handle this session
   TRACE(FORK,"XrdROOTMgr::Validate: forking external proofsrv");
   int pid = -1;
   if (!(pid = sched->Fork("proofsrv"))) {

      if (logfile.length() > 0 && fLogger) {
         // Log to the session log file from now on
         fLogger->Bind(logfile.c_str());
         // Transfer the info to proofserv
         char *ev = new char[strlen("ROOTPROOFLOGFILE=") + logfile.length() + 2];
         sprintf(ev, "ROOTPROOFLOGFILE=%s", logfile.c_str());
         putenv(ev);
         // Create .rootrc
         FILE *frc = fopen(rootrc.c_str(),"w");
         if (frc) {
            fprintf(frc, "Proof.DebugLevel: 1\n");
            fclose(frc);
         }
         // Transfer the info to proofserv
         ev = new char[strlen("ROOTRCFILE=") + rootrc.length() + 2];
         sprintf(ev, "ROOTRCFILE=%s", rootrc.c_str());
         putenv(ev);
      }

      char *argvv[6] = {0};

      // start server
      argvv[0] = (char *)r->PrgmSrv();
      argvv[1] = (char *)"proofserv";
      argvv[2] = (char *)"xpd";
      argvv[3] = (char *)"test";
      if (debug) {
         argvv[4] = (char *)"1";
         argvv[5] = 0;
      } else {
         argvv[4] = 0;
         argvv[5] = 0;
      }

      // Set basic environment for proofserv
      if (XrdProofdProofServMgr::SetProofServEnv(fMgr, r) != 0) {
         TRACE(XERR, " SetProofServEnv did not return OK - EXIT");
         exit(1);
      }

      // Set Open socket
      char *ev = new char[25];
      sprintf(ev, "ROOTOPENSOCK=%d", fp[1]);
      putenv(ev);

      // Prepare for execution: we need to acquire the identity of
      // a normal user
      if (!getuid()) {
         XrdProofUI ui;
         if (XrdProofdAux::GetUserInfo(geteuid(), ui) != 0) {
            TRACE(XERR, "could not get info for user-id: "<<geteuid());
            exit(1);
         }

         // acquire permanently target user privileges
         if (XrdSysPriv::ChangePerm((uid_t)ui.fUid, (gid_t)ui.fGid) != 0) {
            TRACE(XERR, "can't acquire "<<ui.fUser <<" identity");
            exit(1);
         }

      }

      // Run the program
      execv(r->PrgmSrv(), argvv);

      // We should not be here!!!
      TRACE(XERR, "returned from execv: bad, bad sign !!!");
      exit(1);
   }

   // parent process
   if (pid < 0) {
      TRACE(XERR, "forking failed - exit");
      close(fp[0]);
      close(fp[1]);
      return -1;
   }

   // now we wait for the callback to be (successfully) established
   TRACE(FORK, "test server launched: wait for protocol ");

   // Read protocol
   int proto = -1;
   struct pollfd fds_r;
   fds_r.fd = fp[0];
   fds_r.events = POLLIN;
   int pollRet = 0;
   // We wait for 60 secs max (30 x 2000 millisecs): this is enough to
   // cover possible delays due to heavy load
   int ntry = 30;
   while (pollRet == 0 && ntry--) {
      while ((pollRet = poll(&fds_r, 1, 2000)) < 0 &&
             (errno == EINTR)) { }
      if (pollRet == 0)
         TRACE(DBG, "receiving PROOF server protocol number: waiting 2 s ...");
   }
   if (pollRet > 0) {
      if (read(fp[0], &proto, sizeof(proto)) != sizeof(proto)) {
         TRACE(XERR, "problems receiving PROOF server protocol number");
         return -1;
      }
   } else {
      if (pollRet == 0) {
         TRACE(XERR, "timed-out receiving PROOF server protocol number");
      } else {
         TRACE(XERR, "failed to receive PROOF server protocol number");
      }
      return -1;
   }

   // Set valid, record protocol and update export string
   r->SetValid((kXR_int16) ntohl(proto));

   // Cleanup files
   if (logfile.length() > 0 && !debug) {
      if (unlink(logfile.c_str()) != 0) {
         TRACE(XERR, "problems unlinking "<<logfile<<"; errno: "<<errno);
      }
   }
   if (unlink(rootrc.c_str()) != 0) {
      TRACE(XERR, "problems unlinking "<<rootrc<<"; errno: "<<errno);
   }

   // Cleanup
   close(fp[0]);
   close(fp[1]);

   // We are done
   return 0;
}

//______________________________________________________________________________
XrdOucString XrdROOTMgr::ExportVersions(XrdROOT *def)
{
   // Return a string describing the available versions, with the default
   // version 'def' markde with a '*'

   XrdOucString out;

   // Generic info about all known sessions
   std::list<XrdROOT *>::iterator ip;
   for (ip = fROOT.begin(); ip != fROOT.end(); ++ip) {
      // Flag the default one
      if (def == *ip)
         out += "  * ";
      else
         out += "    ";
      out += (*ip)->Export();
      out += "\n";
   }

   // Over
   return out;
}

//______________________________________________________________________________
XrdROOT *XrdROOTMgr::GetVersion(const char *tag)
{
   // Return pointer to the ROOT version corresponding to 'tag'
   // or 0 if not found.

   XrdROOT *r = 0;

   std::list<XrdROOT *>::iterator ip;
   for (ip = fROOT.begin(); ip != fROOT.end(); ++ip) {
      if ((*ip)->MatchTag(tag)) {
         r = (*ip);
         break;
      }
   }

   // Over
   return r;
}
