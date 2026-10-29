# ─────────────────────────────────────────────────────────────────────────────────────
# observer_16k.py — Port Obserwatora i Audytor Rejestru 16-Kubitowego (1_000_000_000)
# Licencja: CC BY-NC-ND 4.0 | Łukasz Włodarski | System Labs (Koszalin, 2026)
# ─────────────────────────────────────────────────────────────────────────────────────

import time
import sys
import amsin_quantum_16k

def main():
    total_16k_loops = 1_000_000_000

    print("==============================================================================================================")
    print("                 SYSTEM LABS — INTERFEJS KASKADY 16-KUBITOWEJ CORE v4.05         ")
    print("==============================================================================================================")
    print("[INIT] Odbezpieczanie pasywnego portu nasłuchu 1495 dla telemetrii P3...")
    
    amsin_quantum_16k.start_telemetry_listener()
    
    print(f"[INIT] Rozruch jądra OpenMP dla rejestru 16-kubitowego (D = 65,536 stanów geometrycznych)...")
    print(f"[INIT] Dystans pomiarowy: {total_16k_loops:,} iteracji masowych w Cache L3 [Zero-RAM Mode]...")
    print("[WAIT] Trwa ewolucja nieliniowa potoku wektorowego... Czekaj na uziemienie danych...")

    start_time = time.perf_counter()
    amsin_quantum_16k.execute_quantum_16k_simulation(total_16k_loops)
    total_elapsed = time.perf_counter() - start_time

    telemetry = amsin_quantum_16k.get_quantum_16k_telemetry(float(total_16k_loops))

    total_gates = total_16k_loops * 16
    gate_time_ms = (total_elapsed / total_gates) * 1000.0

    print("\n==============================================================================================================")
    print("                     RAPORT TELEMETRYCZNY KASKADY 16-KUBITOWEJ — SYSTEM LABS P3")
    print("==============================================================================================================")
    print(f" Całkowita liczba kroków kaskady      : {total_16k_loops:,}")
    print(f" Efektywny czas procesora (Krzem)     : {total_elapsed:.4f} sekund")
    print(f" Globalna przepustowość maszyn        : {total_16k_loops / total_elapsed / 1_000_000.0:.3f} MOPS/s")
    print(" -------------------------------------------------------------------------------------------------------------")
    print(f" MATEMATYCZNY ŚLAD REJESTRU Tr(rho)    : {telemetry.get('trace_16k', 0.0):.12f}  (Szczelność dynamiczna)")
    print(f" GLOBALNA KOHERENCJA P3 (COHERENCE)   : {telemetry.get('global_coherence', 0.0):.12f}")
    print(f" MATEMATYCZNA CZYSTOŚĆ REJESTRU (P)   : {telemetry.get('purity_p3', 1.0):.12f}  (Stan czysty P3)")
    print(" -------------------------------------------------------------------------------------------------------------")
    print(f" Czas wykonania bramki Hadamarda/CNOT : {gate_time_ms:.12f} ms")
    print(f" Dynamiczny narzut pamięci RAM         : {telemetry.get('ram_allocation_bytes', 0.0):.2f} bajtów (ZERO-RAM Mode)")
    print(" -------------------------------------------------------------------------------------------------------------")
    print(" POPULACJE BRZEGOWE KASKADY 16-BITOWEJ (STAN SPLĄTANIA MAKROSKOPIJNEGO):")
    print(f"  Populacja stanu czystego |0000000000000000> : {telemetry.get('pop_0000_16k', 0.0):.12f}")
    print(f"  Populacja stanu pełnego   |1111111111111111> : {telemetry.get('pop_1111_16k', 0.0):.12f}")
    print(" -------------------------------------------------------------------------------------------------------------")
    print(" MONITORY BEZPIECZEŃSTWA HARDWARE'U STANDARDU 585:")
    print(f"  Aktywacje pasywnego pasma chłodzenia ZIBI : {telemetry.get('zibi_triggers', 0):,} razy")
    print(f"  Wymuszone wydechy kompensacji ML-EXHAUST  : {telemetry.get('ml_exhausts', 0):,} razy")
    print("==============================================================================================================")

if __name__ == "__main__":
    main()


