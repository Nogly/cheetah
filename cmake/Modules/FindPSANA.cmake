if(NOT ANA_RELEASE)
find_file(ANA_RELEASE ana-current /davinci/psana/g/psdm/portable/sw/releases/ /reg/g/psdm/sw/releases/ /cfel/common/slac/reg/g/psdm/portable/sw/releases/)
endif(NOT ANA_RELEASE)

message(STATUS "ANA_RELEASE is ${ANA_RELEASE}")

IF(EXISTS ${ANA_RELEASE}/../../../data/ExpNameDb/experiment-db.dat)
	SET(ANA_SIT_DATA ${ANA_RELEASE}/../../../data/ CACHE PATH "Path equivalent to $SIT_DATA")
ELSE(EXISTS ${ANA_RELEASE}/../../../data/ExpNameDb/experiment-db.dat)
	find_path(ANA_SIT_DATA ExpNameDb  DOC "Path equivalent to $SIT_DATA")
ENDIF(EXISTS ${ANA_RELEASE}/../../../data/ExpNameDb/experiment-db.dat)

file(READ /etc/redhat-release redhat_release)
string(REGEX MATCH " 6\\." rhel6 ${redhat_release})
string(REGEX MATCH " 5\\." rhel5 ${redhat_release})
if(rhel6)
  set(ANA_ARCH "x86_64-rhel6-gcc44-opt" CACHE STRING "ana architecture to be used")
endif(rhel6)
if(rhel5)
  set(ANA_ARCH "x86_64-rhel5-gcc41-opt" CACHE STRING "ana architecture to be used")
endif(rhel5)


LIST(APPEND ana_libs ErrSvc PSTime rt PSEvt AppUtils acqdata andordata bld camdata compressdata controldata
  cspad2x2data cspaddata encoderdata epics evrdata fccddata fexampdata flidata gsc16aidata indexdata
  ipimbdata lusidata oceanopticsdata opal1kdata orcadata phasicsdata pnccddata princetondata pulnixdata
  quartzdata timepixdata usdusbdata xampsdata xtcdata ConfigSvc MsgLogger PSEnv RootHistoManager PSHist
  ExpNameDb psddl_psana psana Core Cint RIO Net Hist Graf Graf3d Gpad Tree Rint Postscript Matrix Physics MathCore Thread
  m dl)

foreach(ana_lib IN LISTS ana_libs)
	# Clear variable first
	SET(ANA_${ana_lib}_LIBRARY "ANA_${ana_lib}_LIBRARY-NOTFOUND" CACHE INTERNAL "Internal" FORCE)
	find_library(ANA_${ana_lib}_LIBRARY ${ana_lib} ${ANA_RELEASE}/arch/${ANA_ARCH}/lib/)
	SET(ANA_${ana_lib}_LIBRARY ${ANA_${ana_lib}_LIBRARY} CACHE INTERNAL "Internal" FORCE)
	message(STATUS "Found ${ana_lib} in ${ANA_${ana_lib}_LIBRARY}")
#	mark_as_advanced(ANA_${ana_lib}_LIBRARY)
	LIST(APPEND PSANA_LIBRARIES ${ANA_${ana_lib}_LIBRARY})
endforeach(ana_lib)

#clear var
SET(PSANA_INCLUDES)
LIST(APPEND PSANA_INCLUDES ${ANA_RELEASE}/include ${ANA_RELEASE}/arch/${ANA_ARCH}/geninc)