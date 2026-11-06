# analysis: hardware-free logic (RMS, thresholds, hysteresis, qualification), T08-T09.
# No REQUIRES / PRIV_REQUIRES: it must compile with plain C11, so it asks for nothing from ESP-IDF.
idf_component_register(SRCS "analysis.c"
                       INCLUDE_DIRS "include")
