#include"unpifiplus.h"
#include"myhdr.h"

FTmsg*  FTmsgton(FTmsg *DG)
{
  DG->seqno=htonl(DG->seqno);
  DG->ts=htonl(DG->ts);
  DG->wnd=htonl(DG->wnd);

  return DG;
}
FTmsg* ntoFTmsg(FTmsg *DG)
{
  DG->seqno=ntohl(DG->seqno);
  DG->ts=ntohl(DG->ts);
  DG->wnd=ntohl(DG->wnd);

  return DG;
}
