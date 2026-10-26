# G719: privacy-safe NMEA health aggregation from real gpsd logcat.
# INPUT: logcat -d -v raw -s GpsOutputNmea:D '*:S'
# OUTPUT: statuses and signal counters ONLY; no GPS coordinates, APN or identities.
# Each GSV line can list 1-4 satellites. A blank C/N0 is NOT a tracked SV.
BEGIN {
    sample = 0; gsv_count=0; total_inview=0; snr_empty=0; snr_zero=0;
    snr_positive=0; snr_good=0; peak=0; gsa_count=0; gga_count=0;
    rmc_count=0; gga_no_fix=0; rmc_invalid=0; gsa_no_fix=0;
    last_gsa="-"; last_gga_quality="-"; last_gga_used="-";
    last_rmc="-"; last_gsv="-"; utc="-"; gsv_total_entries=0;
}
{
    pos=index($0,"$");
    if (!pos) next;
    sentence=substr($0,pos);
    n=split(sentence,f,",");
    msg=f[1];
    # Never echo raw sentence: GGA and RMC may contain coordinates.
    if (msg=="$GPGSV" || msg=="$GLGSV" || msg=="$GNGSV" || msg=="$GQGSV") {
        gsv_count++;
        if (f[4] ~ /^[0-9]+$/) last_gsv=f[4];
        for (j=5; j+3<=n; j+=4) {
            if (f[j] !~ /^[0-9]+$/) continue;
            gsv_total_entries++;
            cn=f[j+3]; sub(/\*.*/, "", cn); sub(/\r.*/, "", cn);
            if (cn=="") snr_empty++;
            else if (cn ~ /^[0-9]+$/) {
                if (cn+0==0) snr_zero++;
                else {
                    snr_positive++;
                    if (cn+0>=25) snr_good++;
                    if (cn+0>peak) peak=cn+0;
                }
            }
        }
    } else if (msg=="$GPGSA" || msg=="$GNGSA" || msg=="$GLGSA") {
        gsa_count++;
        last_gsa=f[3];
        if (f[3]=="1") gsa_no_fix++;
    } else if (msg=="$GPGGA" || msg=="$GNGGA") {
        gga_count++;
        last_gga_quality=f[7]; last_gga_used=f[8];
        if (f[7]=="0") gga_no_fix++;
        if (f[2] ~ /^[0-9]+(\.[0-9]+)?$/) utc=f[2];
    } else if (msg=="$GPRMC" || msg=="$GNRMC") {
        rmc_count++;
        last_rmc=f[3];
        if (f[3]=="V") rmc_invalid++;
    }
}
END {
    print "G719_PRIVACY_SAFE_NMEA=1";
    print "LOGCAT_BUFFERED_NOT_REALTIME=1";
    print "GSV_FRAMES=" gsv_count;
    print "SATELLITES_IN_VIEW_LAST_REPORTED=" last_gsv;
    print "SATELLITE_ENTRIES_INSPECTED=" gsv_total_entries;
    print "CN0_EMPTY_ENTRIES=" snr_empty;
    print "CN0_ZERO_ENTRIES=" snr_zero;
    print "CN0_POSITIVE_ENTRIES=" snr_positive;
    print "CN0_AT_LEAST_25_DBHZ_ENTRIES=" snr_good;
    print "CN0_MAX_DBHZ=" peak;
    print "GSA_NO_FIX_FRAMES=" gsa_no_fix;
    print "GSA_FRAMES=" gsa_count;
    print "LAST_GSA_MODE=" last_gsa;
    print "GGA_NO_FIX_FRAMES=" gga_no_fix;
    print "GGA_FRAMES=" gga_count;
    print "LAST_GGA_FIX_QUALITY=" last_gga_quality;
    print "LAST_GGA_SATS_USED=" last_gga_used;
    print "RMC_INVALID_FRAMES=" rmc_invalid;
    print "RMC_FRAMES=" rmc_count;
    print "LAST_RMC_VALIDITY=" last_rmc;
    print "LAST_GGA_UTC_TIME=" utc;
    if (gsv_count==0 || gga_count==0 || gsa_count==0 || rmc_count==0)
        print "VERDICT=INSUFFICIENT_NMEA";
    else if (last_gga_quality=="0" && last_gsa=="1" && last_rmc=="V" &&
             snr_positive==0 && (snr_empty+snr_zero)>0)
        print "VERDICT=NO_FIX_NO_TRACKED_SIGNAL_IN_BUFFERED_SAMPLE";
    else if (last_gga_quality!="0" && last_rmc=="A")
        print "VERDICT=NMEA_POSITION_VALID";
    else
        print "VERDICT=MORE_RF_AND_NAV_DATA_NEEDED";
}
