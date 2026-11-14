# Paralelni Hash Join na višejezgrenom CPU-u (C++20 / OpenMP)

Projekat iz predmeta **Paralelni računarski sistemi (PRS)**  
**Autori:** Amer Zec, Amina Bećirević, Amil Lujinović  
**Okruženje:** C++20, OpenMP, CMake, Apple Silicon (ARM64)

---

## 1. Teorijska osnova i motivacija

Operacija spajanja relacija po jednakosti ključeva (*Equi-Join*) predstavlja jednu od računarski i memorijski najzahtjevnijih operacija u modernim sistemima za upravljanje bazama podataka (*DBMS*). U analitičkim radnim opterećenjima (*OLAP*), gdje se velike tabele obrađuju direktno u glavnoj memoriji (*In-Memory Databases*), ukupno vrijeme izvršavanja upita dominantno zavisi od efikasnosti algoritma spajanja i njegovog iskorištavanja memorijske hijerarhije procesora.

Klasični pristup spajanja ugniježđenim petljama (*Nested Loop Join*) ima kvadratnu vremensku složenost $\mathcal{O}(\vert{}R\vert{} \cdot \vert{}S\vert{})$, što ga čini neupotrebljivim za velike skupove podataka. Nasuprot tome, **Hash Join** svodi složenost na linearnu $\mathcal{O}(\vert{}R\vert{} + \vert{}S\vert{})$ kroz dvije odvojene faze:
1. **Build faza:** Manja tabela (oznaka $R$) se skenira i od njenih ključeva se u radnoj memoriji formira hash tabela.
2. **Probe faza:** Veća tabela (oznaka $S$) se sekvencijalno prolazi, te se za svaku torku izračunava hash vrijednost i pretražuju odgovarajući lanci kolizija u hash tabeli.

Međutim, na savremenim višejezgrenim procesorima performanse *In-Memory Hash Join* algoritma nisu ograničene brzinom aritmetičkih instrukcija, već **latencijom pristupa radnoj memoriji, promašajima u L1/L2 kešu i TLB (*Translation Lookaside Buffer*) promašajima** (Shatdal et al., 1994; Manegold et al., 2002). Kada hash tabela premaši kapacitet brzih privatnih keševa procesora, nasumični pristupi tokom *Build* i *Probe* faze uzrokuju zasićenje memorijske magistrale (*memory bandwidth saturation*).

U naučnoj literaturi izdvajaju se dvije fundamentalno različite arhitekture za paralelizaciju *Hash Join* algoritma na dijeljenoj memoriji:
* **Pristup bez particionisanja (*No-Partitioning / Shared Hash Table*):** Sve niti paralelno grade i pretražuju jednu zajedničku hash tabelu u RAM-u koristeći atomske instrukcije (*lock-free* sinhronizacija) i privatne izlazne bafere radi izbjegavanja lažnog dijeljenja (*false sharing*) (Blanas et al., 2011).
* **Pristup zasnovan na particionisanju (*Partitioned / Radix Hash Join*):** Ulazne tabele se u prvom koraku pomoću *Radix* bitova hash vrijednosti dijele na manje, nezavisne particije dimenzionisane tako da svaka particija i njena pripadajuća lokalna hash tabela u potpunosti stanu u brzi L1/L2 keš procesora, čime se eliminiše potreba za sinhronizacijom u drugoj fazi (Balkesen et al., 2013; Schuh et al., 2016).

---

## 2. Vizija projekta i obuhvat istraživanja

Vizija ovog projekta je izgradnja čistog, determinističkog i keš-svjesnog (*cache-conscious*) C++20/OpenMP okvira za eksperimentalnu evaluaciju *In-Memory Hash Join* algoritama na višejezgrenom CPU-u, bez oslanjanja na ručnu SIMD vektorizaciju, s fokusom na paralelizam na nivou niti i organizaciju podataka u memoriji.

### Šta projektom nastojimo obraditi:
1. **Asimptotska i keš-svjesna optimizacija sekvencijalnog izvršavanja:**
   * Poređenje $\mathcal{O}(\vert{}R\vert{} \cdot \vert{}S\vert{})$ *Nested Loop Join* baze sa $\mathcal{O}(\vert{}R\vert{} + \vert{}S\vert{})$ *Hash Join* algoritmom.
   * Poređenje dvije strukture podataka za rješavanje kolizija u memoriji: **Flat Chaining** (dva neprekidna niza indeksa `head` i `next` bez dinamičke alokacije čvorova i *pointer chasing-a*) naspram **Open Addressing** pristupa sa linearnim probiranjem (*Linear Probing*).
2. **Dva konkurentska OpenMP paralelna modela:**
   * **Varijanta A (*Parallel Shared Hash Table*):** *Lock-free* paralelna izgradnja zajedničke tabele pomoću hardverske atomske operacije `std::atomic<int32_t>::exchange` i dinamička raspodjela pretrage (`schedule(dynamic, 4096)`).
   * **Varijanta B (*Parallel Partitioned Radix Hash Join*):** Dvoprolazno paralelno particionisanje na $2^B$ particija (lokalni histogrami + prefiksne sume) praćeno izolovanom izgradnjom i pretragom lokalnih hash tabela unutar L2 keša procesora.
3. **Uticaj neravnomjerne raspodjele podataka (*Data Skew*):**
   * Analiza degradacije performansi usljed nebalansiranog opterećenja niti (*load imbalance*) i memorijskog zagušenja pri Zipfian distribuciji ključeva ($\theta \in [0, 1.1]$), te ispitivanje tehnika za ublažavanje skew-a (razbijanje vrućih particija, dvoprolazna prealokacija izlaza i agregacijski `COUNT(*)` način rada).
4. **Osjetljivost arhitekturnih parametara:**
   * Analiza uticaja broja niti ($p \in \{1, 2, 4, 8\}$) na heterogenoj arhitekturi (4 *Performance* + 4 *Efficiency* jezgre) te uticaja broja *Radix* bitova ($B \in \{4, 5, 6, 7, 8\}$) na iskorištenost L1/L2 keša i TLB-a.

---

## 3. Konačni izlaz i grafička vizuelizacija rezultata

Konačni izlaz sistema sastoji se od:
* **Verifikovanog C++20/OpenMP izvornog koda** sa automatizovanim testnim paketom koji garantuje 100% poklapanje rezultata svih paralelnih varijanti sa sekvencijalnim referentnim algoritmom.
* **Strukturiranog CSV izvještaja (`results.csv`)** generisanog iz višestrukih ponavljanja (uz *warm-up* prolaz, srednju vrijednost i standardnu devijaciju) sa razloženim vremenima po fazama: `Partition`, `Build`, `Probe`, `Merge/Overhead` i `Total`, uz izračunato ubrzanje $S(p) = T_{\text{seq}} / T(p)$ i paralelnu efikasnost $E(p) = \frac{S(p)}{p} \times 100\%$.

### Planirani grafički prikazi rezultata:
1. **Asimptotsko poređenje (Log-skala):** Rast vremena izvršavanja *Nested Loop Join* vs. *Seq Hash Join* sa povećanjem ulaznih tabela.
2. **Uticaj organizacije hash tabele:** Grupisani stubičasti dijagram *Flat Chaining* vs. *Open Addressing* po fazama (*Build* i *Probe*) na uniformnim i *skewed* podacima.
3. **Krivulje paralelnog ubrzanja (*Speedup*):** Skalabilnost Varijante A i Varijante B kroz $1, 2, 4$ i $8$ niti u odnosu na idealnu linearnu liniju ($y = x$), uz isticanje superlinearnog efekta L2 keša i prelaza na *Efficiency* jezgre.
4. **Profilisanje faza izvršavanja (*Stacked Bar Chart*):** Udio faza *Partition*, *Build*, *Probe* i *Merge/Overhead* kroz različit broj niti radi identifikacije memorijskih uskih grla.
5. **Analiza osjetljivosti na *Data Skew* i *Radix* bitove:** Ponašanje oba algoritma pri promjeni Zipfian parametra $\theta$ te pronalazak optimalnog broja particija $2^B$ za L2 keš ciljnog procesora.

---

## 4. Struktura repozitorija

```text
hash-join-prs/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── types.hpp                           # Kompaktne strukture Tuple (24 B) i JoinResult (32 B)
│   ├── data_generator.hpp                  # Deterministički generator (Uniform, Zipfian, rubni slučajevi)
│   ├── nested_loop_join.hpp                # Referentni O(|R|*|S|) algoritam za provjeru tačnosti
│   ├── seq_hash_join.hpp                   # Sekvencijalni Hash Join (Flat Chaining: head + next)
│   ├── seq_hash_join_open_addr.hpp         # Sekvencijalni Hash Join (Open Addressing / Linear Probing)
│   ├── parallel_shared_hash_join.hpp       # Varijanta A: OpenMP Shared Hash Table (lock-free atomic)
│   ├── parallel_partitioned_hash_join.hpp  # Varijanta B: OpenMP Partitioned Radix Hash Join (64 particije)
│   └── verification.hpp                    # Automatska verifikacija ispravnosti rezultata
└── src/
    └── main.cpp                            # Pokretanje testova tačnosti i eksperimentalnih mjerenja