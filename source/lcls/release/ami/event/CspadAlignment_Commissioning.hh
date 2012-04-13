//
//  CSPAD commissioning detector 
//    only quadrant 2 populated => replicate everywhere
//

static const Ami::Cspad::QuadAlignment qproto = {
  {  { {   {0,       0},
	   {43970,   7},
	   {43978,   21427},
	   {4,       21427} },
       {547,	21846} },
     { {   {17,      23461},
	   {43996,   23218},
	   {44117,   44667},
	   {134,     44906} },
       {690,	45312} },
     { {   {632,     47000},
	   {1137,    90967},
	   {22579,   90723},
	   {22071,   46766} },
       {22975,	90179} },
     { {   {24031,   46694},
	   {24413,   90681},
	   {45851,   90478},
	   {45471,   46506} },
       {46253,	89931} },
     { {   {47761,   90117},
	   {91725,   89497},
	   {91425,   68067},
	   {47454,   68689} },
       {90871,	67667} },
     { {   {47401,   66724},
	   {91369,   66127},
	   {91078,   44700},
	   {47107,   45286} },
       {90523,	44299} },
     { {   {44135,   701},
	   {44389,   43268},
	   {65838,   43131},
	   {65577,   829} },
       {66245,	42592} },
     { {   {67520,   911},
	   {67786,   43050},
	   {89220,   42921},
	   {88952,   1040} },
       {89628,	42372} } }
  };

static const Ami::Cspad::QuadAlignment qalign_def[] = 
  { qproto, qproto, qproto, qproto };