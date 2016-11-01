
# There is a budget for many file descriptors, just in case.
# But, out of all possible sockets, there might be only a handful
# that have ever been used.  Reporting on thousands of slots that
# have been allocated but never used just leads to visual clutter.
# So, we want to measure the range of sockets that actually have
# some recorded history.
#
# We record the range, [ $min_sock .. $max_sock ] for use in other
# functions that visit all sockets.

define measure_sock_xports
    set $min_sock = -1
    set $max_sock = -1
    set $max_xnr = xports_size
    set $xnr = 0
    printf "Measuring socket SFR ...\n"
    while ($xnr < $max_xnr)
        set $ts = sock_sfr[$xnr].sfr_timestamp
        if ($ts != 0)
            if ($min_sock == -1)
                set $min_sock = $xnr
            end
            set $max_sock = $xnr
        end
        if ($xnr % 100 == 0)
            printf "."
        end
        set $xnr = $xnr + 1
    end
    printf "\n"
end

# Show per socket history for all sockets.
#
# For each socket that has any history, show the { timestamp, thread, psr }
# of the last allocation of an SVCXPRT data structure for that socket
# (file dscriptor).

define dump_sock_sfr
    measure_sock_xports
    set $xnr = $min_sock
    set $plnr = 0
    while ($xnr <= $max_sock)
        set $ts = sock_sfr[$xnr].sfr_timestamp
        if ($ts != 0)
            # print sock_sfr[$xnr]
            if ($plnr == 0)
                printf "sock) hi-res timestamp        thread-id psr\n"
                printf "----- ---------------- ---------------- ---\n"
            end
            set $tid = sock_sfr[$xnr].sfr_tid
            set $psr = sock_sfr[$xnr].sfr_psr
            printf "%4d) %16x %16x %3u\n", $xnr, $ts, $tid, $psr
            set $plnr = $plnr + 1
        end
        set $xnr = $xnr + 1
    end
end
