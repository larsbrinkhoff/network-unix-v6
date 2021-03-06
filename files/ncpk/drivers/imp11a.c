#
#include "param.h"
#include "user.h"
#include "buf.h"
#include "net_net.h"
#include "net_netbuf.h"
#include "imp11a.h"
#include "imp.h"
#include "file.h"
#include "net_ncp.h"
#include "net_contab.h"



/*name:
	imp_init
function:
	to initialize the imp interface hardware, and various software
	parameters.

algorithm:
	set fixed input addresses
	set fixed output addresses
	reset the imp interface
	stop strobing the host master ready bit
	enable interrupts
	clear any extended memory address bits
	build and send 3 nops to the imp
	built fixed parameters in the imp input leader

parameters:
	none

returns:
	an initialized imp

globals:
	impncp=
	impistart=
	impiend=
	impostart=
	impoend=
	imp.nrcv=

calls:
	impstrat
	impldrd

called by:
	impopen

history:
	initial coding 10/5/75 S. F. Holmgren

*/

imp_init()
{
	int i;
	extern lbolt;
	
	imp_reset();
	HMR_interval = 60;
	set_HMR();	/* start strobing host master ready */
	while( IMPADDR->istat&imasrdy)
		sleep (&lbolt, 0);	/* until imp says is up */

	/* Turn off further strobing, to avoid data lossage caused
	 * by BIS'ing the input CSR during data transfers.  --KLH
	 */
	HMR_interval = 0;	/* KLH 4/79 */

	/* enable interrupts */
	IMPADDR->istat =| iienab;	/* KLH 4/79 -- OR the enable bits, */
	IMPADDR->ostat =| iienab;	/* to avoid needless HMR yo-yoing. */

}
/*name:
	imp_output

function:
	Takes buffers( see buf.h ) linked into the impotab active
	queue and applies them to the imp interface.

algorithm:
	check to see if the queue is empty if so return
	set the active bit
	enable output interrupts and the endmsg bit( imp interface manual)
	calculate then end address and perform the mod 2 function
	on the calculated address( again see imp interface manual)
	load the start output address register
	load the end output address register which is also an implied
	go

parameters:
	none

returns:
	nothing

globals:
	impotab.d_actf
	(buffer)

calls:
	nothing

called by:
	impstrat
	imp_oint

history:
	initial coding 1/7/75 by S. F. Holmgren

*/


imp_output()
{
	register struct buf *bp;
	register char *endaddr;
	register char *addr;

	
	if((bp = impotab.d_actf) == 0)
		return;			/* return nothing to do */

	impotab.d_active++;		/* something to do set myself active */

	/* set or reset disable endmsg according to user wishes */
	IMPADDR -> ostat = iienab;
	if (bp->b_blkno)
		IMPADDR->ostat =&  ~oendmsg;
	else
		IMPADDR->ostat =| oendmsg;

	IMPADDR->spo = bp->b_addr;	/* load start addr */
	IMPADDR->owcnt = - (bp->b_wcount >> 1);
	IMPADDR->ostat =| igo;

	
}



/*name:
	impread

function:
	to apply an input area to the input side of the imp interface

algorithm:
	say not doing leader read
	load start input register with address passed
	load end input register with address+size passed

parameters:
	addr - start address in which to store imp input data
	size - available number of bytes in which to store said data

returns:
	nothing

globals:
	impleader=

calls:
	nothing

called by:
	flushimp
	ihbget

history:
	initial coding 1/7/75 by S. F. Holmgren

*/

impread( addr,size )		/* start a read from imp into data area */
{
	spl_imp ();			/* not to be interrupted, please */
	impi_adr = addr;
	impi_wcnt = size>>1;
	IMPADDR->spi = addr;
	IMPADDR->iwcnt = -impi_wcnt;
	IMPADDR->istat =| (igo | iwrtenble);
	

}

/*name:
	imp_oint

function:
	This is the imp output interrupt.
	Handles both system type buffers( buf.h ) and the network msgs
	If it finds that a net message is being sent, steps to the
	next buffer in the message and transmits that.
	If all buffers of a net message or a standard system buffer
	has been encountered, it is freed and the output side is
	started once again.

algorithm:
	Check for unexpected interrupts from the imp
	Get the first buffer from the output queue
	If there was an error in output, it is indicated, and the
	buffer returned.
	If the buffer embodies a network message( B_SPEC ) and
	the last buffer of that message has not been sent,
		Get to next buffer in net message
	        Update w_count for that buffer
		Update next buffer pointer(av_forw) so next
		time come through things will be easier.
		Decide whether to set endmsg bit on not.
		Start up output side
	otherwise
		get next buffer in output queue
		if this was a net message give it back to the system
		otherwise say that the system buffer( buf.h ) is done
	if there is nothing to do clean up a little
	otherwise
	start up the output side again.

parameters:
	none

returns:
	nothing

globals:
	impotab.d_active
	buffer->b_flags=
	buffer->b_error=
	buffer->b_addr=
	buffer->b_wcount=
	buffer->av_forw=
	buffer->b_blkno=
	impotab->d_actf=

calls:
	imp_output
	freemsg
	iodone

called by:
	system

history:
	initial coding 1/7/75 by S. F. Holmgren

*/

i11a_oint()
{
	char errbits;

	if (impotab.d_active == 0) { /* ignore unexpected interrupts */
		printf ("\nIMP: Phantom Out Int\n");
		return;
	}

	errbits = 0;
	if( IMPADDR->ostat & itimout )	/* timeout errror */
	{
		errbits++;
		printf ("\nIMP: Output Timeout\n");
	}
	imp_odone (errbits);
}
/*
name:
	imp_iint

function:
	Simply wakes up the input handling process to tell something
	has arrived from the network

algorithm:
	calls wakeup

parameters:

	none


returns:
	i hope so

globals:
	imp_proc

calls:
	wakeup		to start input handling process running

called by:
	hardware

history:
	coded by Steve Holmgren 04/28/75


*/

i11a_iint()
{
	imp_stat.error = (IMPADDR->istat < 0) & (IMPADDR->iwcnt != 0);
	imp_stat.inpendmsg = IMPADDR->istat & iendmsg;
	imp_stat.inpwcnt = IMPADDR->iwcnt;
	wakeup( &imp );
}
/*name:

/*name:
	set_HMR

function:
	To repeatedly tickle the host master ready bit of the imp interface

algorithm:
	if someone hasnt set HMR_interval to zero ( should i go away?? )
		set the host master ready bit
		setup timeout so that i am called again

parameters:
	none

returns:
	nothing

globals:
	HMR_interval
	hostmaster ready bit=

calls:
	timeout(sys)

called by:
	impopen
	timeout(sys)

history:
	initial coding 1/7/75 by S. F. Holmgren

*/

/* The following procedure is needed for compatibility with
 * a munged kernel clock routine that calls this routine every tic.
 * Why THAT is needed has to do with the fact that some other imp
 * interfaces (eg ILL) need true strobing to maintain host-master-ready and
 * if that is held up by an interrupt-level debugging printf, you lose.
 * Whereas for the IMP11A unrestrained strobing will screw data xfer... sigh.
 * --KLH
 */
clk_HMR()   /* Crock for clock level hacking --KLH 4/79 */
{
}

set_HMR()
{
	if( HMR_interval )
	{
		IMPADDR->istat =| hmasrdy;
		timeout( &set_HMR,0,HMR_interval);	/* restart */
	}else{
		wakeup (&HMR_interval);
	}
}

/*
	spl_imp

function:
	centralize priority setting of imp because of variations
	in interface configurations

algorithm:
	if you can't figure this out you'd better go back and
	start all over again

parameters:
	absolutely none

returns:
	yes - if it doesn't you'll know it!

globals:
	no

calls:
	spl5

called by:
	anyone who doesn't want the imp bothering him

history:
	Added 1 Feb 77 for compatibility with other imp-drivers,
		by JC McMillan

*/

spl_imp()
{
	spl5();
}
/*name:
	imp_reset

function:
	reset interface

algorithm:
	reset host_master ready. Reset all other bits

parameters:
	none

returns:
	nothing

globals:
	none

calls:
	nobody

called by:
	imp_dwn
	imp_init

history:
	initial coding 08 Jan 77 by JSK

*/

imp_reset()
{
	register char *impadd;
	printf("\nIMP:Reset\n");

/* Whoever coded this section and imp_init obviously never tried it,
 * because it unconditionally hangs the NCP - set_HMR isn't called
 * until AFTER imp_reset, so the sleep never wakes up!!
 * Hence, commented out.  --KLH 4/79
	spl6 ();
	HMR_interval = 0;
	sleep (&HMR_interval);
*/
	impadd = IMPADDR;
	impadd -> istat =| irst;
	impadd -> istat =& ~irst;
	impadd -> ostat =| irst;
	impadd -> ostat =& ~irst;
	impadd -> istat = impadd->ostat = 0;
}
   