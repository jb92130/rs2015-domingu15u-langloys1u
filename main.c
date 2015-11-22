//
//  main.c
//  Projet_RS_1
//
//  Created by Jean-Baptiste Dominguez on 18/10/2015.
//  Copyright (c) 2015 Jean-Baptiste Dominguez. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>

#define NB_THREAD 2

typedef enum TYPE_MEMORY_HEAD {
    EMPTY,
    ALLOCATED,
} TYPE_MEMORY_HEAD;

typedef struct memory_head {
    TYPE_MEMORY_HEAD type;
    unsigned int size;
    unsigned int serial;
    struct memory_head* next;
    struct memory_head* prev;
} memory_head;

typedef struct memory_manager {
    unsigned int nb_empty;
    unsigned int max_empty;
    unsigned int size;
    memory_head* first;
    memory_head* last;
} memory_manager;

memory_manager* mm;
unsigned int memory_manager_init = 0;
pthread_mutex_t memory_manager_mutex;

void Mem_Init (unsigned int size) {
    
    int sizeOfRegion = (size/getpagesize()+1)*getpagesize();
    
    printf("Mémoire réservée: %d \n", sizeOfRegion);
    
    // Allocation mémoire auprès du Systeme d'Exploitation
    mm = (memory_manager*) mmap(NULL, sizeOfRegion, PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    
    if (mm == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    
    // On initialise la mémoire avec que des 0
    memset(mm, 0, sizeOfRegion);
    
    // Taille de la page mémoire
    printf("Taille page mémoire: %d \n", getpagesize());
    printf("Position dans la mémoire : %p \n", mm);
    
    // Initialisation du manager de la mémoire
    mm->size = sizeOfRegion-sizeof(memory_manager)-sizeof(memory_head);
    mm->nb_empty = 1;
    mm->max_empty = mm->size;
    mm->first = (memory_head*) ((void*) mm + sizeof(memory_manager));
    mm->last = mm->first;
    
    // Initialisation de la première entete
    mm->first->type = EMPTY;
    mm->first->size = mm->size;
    mm->first->next = NULL;
    mm->first->prev = NULL;
    mm->first->serial = 123456;
    
    // On signale que le manager a été initialisé !
    memory_manager_init = 1;
}

typedef struct memory_search {
    unsigned int num;
    int* sync;
    unsigned int size;
    memory_head** elt;
    pthread_t* threads;
} memory_search;

void* Mem_ThreadSearch (void* arg) {
    // Init
    memory_head* elt = NULL;
    
    // Récupération des paramètres de recherche
    memory_search* ms = (memory_search*) arg;
    
    // Si on est un thread qui recherche par le haut
    if (ms->num == 0) {
        
        // On prend le premier élt
        elt = mm->first;
        
        // Recherche d'un élt correspondant à la taille demandée
        while (elt != NULL && ((int) elt) <= (((int) mm->first) + (mm->size/2))
               && (elt->size < ms->size || elt->type == ALLOCATED) && *(ms->sync) == 0) {
            elt = elt->next;
        }
    }
    // Si on est un thread qui recherche par le bas
    else {
        
        // On prend le dernier élt
        elt = mm->last;
        
        // Recherche d'un élt correspondant à la taille demandée
        while (elt != NULL && ((int) elt) >= (((int) mm->last) - (mm->size/2))
               && (elt->size < ms->size || elt->type == ALLOCATED) && *(ms->sync) == 0) {
            elt = elt->prev;
        }
    }
    
    // Je prends le verrou
    pthread_mutex_lock(&memory_manager_mutex);
    
    // Si aucun autre thread n'a trouvé que mon elt est différent de NULL, c'est que j'ai trouvé
    if (elt != NULL && elt->size >= ms->size && elt->type == EMPTY && *(ms->sync) == 0) {
        
        // Je sauvegarde mes datas et j'indique aux autres threads
        *(ms->sync) = 1;
        *(ms->elt) = elt;
    }
    
    // Je lache le verrou
    pthread_mutex_unlock(&memory_manager_mutex);
    
    pthread_exit(NULL);
}

memory_head* Mem_SearchFree (unsigned int size) {
    
    // Init des conf pour les threads
    int sync = 0;
    pthread_t threads[NB_THREAD];
    memory_head* mh = NULL;
    memory_search* thread_args = (memory_search*) malloc(sizeof(memory_search)*NB_THREAD);
    int thread_res[NB_THREAD];
    
    // Initialisation de la sémaphore pour le multithread
    pthread_mutex_init(&memory_manager_mutex, NULL);
    
    for (int i=0; i<NB_THREAD; i++) {
        thread_args[i].num = i;
        thread_args[i].sync = &sync;
        thread_args[i].size = size;
        thread_args[i].elt = &mh;
        thread_args[i].threads = threads;
    }
    
    // Lancement du thread qui va chercher par le haut de la file
    thread_res[0] = pthread_create(&threads[0], NULL, Mem_ThreadSearch, (void*) &thread_args[0]);
    
    // Lancement du thread qui va chercher par le bas de la file
    thread_res[1] = pthread_create(&threads[1], NULL, Mem_ThreadSearch, (void*) &thread_args[1]);
    
    // On attend la fin des threads de recherche
    for (int i=0; i<NB_THREAD; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Je ferme le sémaphore
    pthread_mutex_destroy(&memory_manager_mutex);
    
    // Je libère mes args car je n'en ai plus besoin :)
    free(thread_args);
    
    // On retourne l'entete libre si trouvé ou NULL sinon
    return mh;
}

void* Mem_GetHeader (void* ptr) {
    
    if (ptr == NULL) return NULL; 

    // Si le pointeur est dans les bornes (Après le manager et le premier header du premier bloc et avant la fin du dernier bloc)
    if (ptr >= ((void*) mm->first + sizeof(memory_head))
        && ptr <= ((void*) mm->last + sizeof(memory_head) + mm->last->size)) {
        
        // On parcourt un octet par octet en remontant pour trouver le header le plus proche
        for (int i=0; ; i++) {
            
            memory_head* m = (memory_head*) (ptr-i);
            
            if (m->serial == 123456) {
                
                int tmp = ((void*) m) + m->size + sizeof(memory_head);
                int ptr_tmp = ptr;
                
                // Si le premier header trouvé a le statut ALLOCATED, alors on retourne vrai (1)
                if (m->type == ALLOCATED
                    && i >= sizeof(memory_head)
                    && ptr_tmp<tmp) {
                    return m;
                }
                // Sinon, si le statut est EMPTY, alors on retourne faux (-1)
                else if (m->type == EMPTY
                         && i >= sizeof(memory_head)) {
                    return NULL;
                }
            }
        }
    }
    return NULL;
}

int Mem_IsValid (void* ptr) {
    
    // Je cherche une entete correspondant à mon pointeur
    void* tmp = Mem_GetHeader(ptr);
    
    // Si mon pointeur d'entete existe alors je retourne 1
    if (tmp != NULL) {
        return 1;
    }
    return -1;
}

int Mem_GetSize (void* ptr) {
    
    // Je cherche une entete correspondant à mon pointeur
    void* tmp = Mem_GetHeader(ptr);
    
    // Si mon pointeur d'entete existe alors je retourne la taille
    if (tmp != NULL) {
        return ((memory_head*) tmp)->size;
    }
    return -1;
}

int Mem_Free (void* ptr) {
    
    // Je cherche une entete correspondant à mon pointeur
    void* tmp = Mem_GetHeader(ptr);
    
    // Si une entete existe pour cette adresse, je libère
    if (tmp != NULL) {
        
        // Récupération de l'entete
        memory_head* mh = (memory_head*) tmp;
        
        // S'il n'a pas de précédent et pas de suivant
        if (mh->prev == NULL && mh->next == NULL) {
            
            // On libère le bloc
            mh->type = EMPTY;
            
            // On met à jour le manager
            mm->nb_empty++;
            if (mh->size > mm->max_empty) {
                mm->max_empty = mh->size;
            }
            
            // On ré-initialise le bloc mémoire avec que des 0
            memset((void*) mh + sizeof(memory_head), 0, mh->size);
            
        }
        // S'il y a un précédent et un suivant EMPTY
        else if (mh->prev != NULL && mh->prev->type == EMPTY && mh->next != NULL && mh->next->type == EMPTY) {
            
            // J'ajoute la taille dans le bloc
            mh->prev->size += (mh->size + mh->next->size + sizeof(memory_head)*2);
            
            // Je fais disparaitre mon bloc courant et le suivant en cassant le serial
            mh->serial = 0;
            mh->next->serial = 0;
            
            // On met à jour le manager
            if (mh->prev->size > mm->max_empty) {
                mm->max_empty = mh->prev->size;
            }
            
            // Je supprime mon bloc de la chaine
            mh->prev->next = mh->next->next;
            if (mh->prev->next != NULL) {
                mh->prev->next->prev = mh->prev;
            }
            
            // On met à jour le manager
            mm->nb_empty--;
            if (mh->next == mm->last) {
                mm->last = mh->prev;
            }
            
            // On ré-initialise le bloc mémoire avec que des 0
            memset((void*) mh->prev + sizeof(memory_head), 0, mh->prev->size);
        }
        // S'il y a un précédent EMPTY
        else if (mh->prev != NULL && mh->prev->type == EMPTY) {
            
            // Je fais disparaitre mon bloc courant en cassant le serial
            mh->serial = 0;
            
            // On supprime l'entete en ajustant les suivants et les précédents
            mh->prev->size += (mh->size + sizeof(memory_head));
            mh->prev->next = mh->next;
            if (mh->next != NULL) {
            	mh->next->prev = mh->prev;
            }
            
            // On met à jour le manager
            if (mh->prev->size > mm->max_empty) {
                mm->max_empty = mh->prev->size;
            }
            if (mh == mm->last) {
                mm->last = mh->prev;
            }
            
            // On ré-initialise le bloc mémoire avec que des 0
            memset((void*) mh->prev + sizeof(memory_head), 0, mh->prev->size);
        }
        // S'il y a un suivant EMPTY
        else if (mh->next != NULL && mh->next->type == EMPTY) {
            
            // Je fais disparaitre mon bloc suivant du courant en cassant le serial
            mh->next->serial = 0;
            
            // On supprime le trou suivant pour fusionner les trous
            mh->type = EMPTY;
            mh->size += (mh->next->size + sizeof(memory_head));
            
            if (mh->next->next != NULL) {
                mh->next->next->prev = mh;
            }
            
            mh->next = mh->next->next;
            
            // On met à jour le manager
            if (mh->size > mm->max_empty) {
                mm->max_empty = mh->size;
            }
            if (mh->next == mm->last) {
                mm->last = mh;
            }
            
            // On ré-initialise le bloc mémoire avec que des 0
            memset((void*) mh + sizeof(memory_head), 0, mh->size);
        }
        // S'il y a un précédant ou/et suivant alloué
        else if ((mh->next!=NULL && mh->next->type == ALLOCATED) || (mh->prev != NULL && mh->prev->type == ALLOCATED)) {
            mh->type = EMPTY;
            
            // On met à jour le manager
            mm->nb_empty++;
            if (mh->size > mm->max_empty) {
                mm->max_empty = mh->size;
            }
            
            // On ré-initialise le bloc mémoire avec que des 0
            memset((void*) mh + sizeof(memory_head), 0, mh->size);
        }
        else {
            return -1;
        }
        
        return 0;
        
    }
    
    return -1;
}



void* Mem_Alloc (unsigned int size) {
    // Init du Mem
    if (memory_manager_init == 0) {
        
        // Init Mem
        Mem_Init(10000);
    }
    
    // Test préliminaire (première élimination des possibilités)
    if (mm->nb_empty > 0 && mm->max_empty > size) {
        
        // On cherche un elt libre
        memory_head* elt = Mem_SearchFree(size);
        
        // On ne trouve pas d'élément libre, on retourne NULL
        if (elt == NULL) {
            return NULL;
        }
        
        // Si je possède un suivant et mon suivant est vide (EMPTY)
        if (elt->next != NULL && (elt->next)->type == EMPTY) {
            
            // Je donne au suivant la quantité que j'ai en trop
            (elt->next)->size += elt->size-size;
            
            // Je change mon statut et j'ajuste la taille de mon bloc par rapport à ce que j'ai donné au suivant
            elt->type = ALLOCATED;
            elt->size = size;
            
            // Je retourne l'adresse
            return (void*) elt + sizeof(memory_head);
        }
        
        // Si je ne possède pas de suivant, je dois en créer un si la taille restante me le permet
        else if (elt->next == NULL) {
            
            // Test si nous avons assez de place pour créer le suivant
            if (elt->size > (size + sizeof(memory_head))) {
                
                // On crée un suivant
                memory_head* mh = (memory_head*) ((void*) elt + sizeof(memory_head) + size);
                
                mh->type = EMPTY;
                mh->size = elt->size - size - sizeof(memory_head);
                mh->prev = elt;
                mh->serial = 123456;
                mh->next = NULL;
                
                // Je change mon statut et j'ajuste la taille de mon bloc par rapport à ce que j'ai donné au suivant
                elt->type = ALLOCATED;
                elt->size = size;
                
                // On informe à l'élément courant qu'il a maintenant un suivant
                elt->next = mh;
                
                // Je mets à jour mon manager concernant le nouveau last
                mm->last = mh;
            }
            // Si je n'ai pas la place de faire un suivant, je lui renvoie un peu plus pour éviter de perdre de la mémoire
            else {
                // Je change mon statut
                elt->type = ALLOCATED;
                
                // Je préviens mon manager d'un empty de moins
                mm->nb_empty--;
            }
            
            // Je retourne l'adresse
            return (void*) elt + sizeof(memory_head);
        }
        
        // Si je possède un suivant et mon suivant est alloué (ALLOCATED)
        else if (elt->next != NULL && ((memory_head*) elt->next)->type == ALLOCATED) {
            
            // Test si nous avons la place de créer un suivant
            if (elt->size > (size + sizeof(memory_head))) {
                
                // On crée un suivant
                memory_head* mh = (memory_head*) ((void*) elt + sizeof(memory_head) + size);
                mh->type = EMPTY;
                mh->size = elt->size - size - sizeof(memory_head);
                mh->prev = elt;
                mh->serial = 123456;
                
                // Le suivant du nouveau élt est le suivant de l'élt courant
                mh->next = elt->next;
                    
                // Le suivant de l'élt a pour précédant le nouveau élt
                ((memory_head*) elt->next)->prev = mh;
                
                // On informe à l'élément courant qu'il a un autre suivant
                elt->next = mh;
                
                // On a crée un élt suivant EMPTY, donc nous avons comme taille de notre élt courant, la taille demandée
                elt->size = size;
            }
            
            // Allocation de l'élt trouvé
            elt->type = ALLOCATED;
            
            // On revoit l'adresse du début de bloc alloué
            return (void*) elt + sizeof(memory_head);
        }
    }
    
    return NULL;
}

void Mem_MemoryHeadPrint (memory_head* mh) {
    printf("---    ADD : %12p ---\n", mh);
    printf("---   PREV : %12p ---\n", mh->prev);
    printf("--- SERIAL : %12d ---\n", mh->serial);
    printf("---   SIZE : %12d ---\n", mh->size);
    if (mh->type == EMPTY) {
        printf("---   TYPE :        EMPTY ---\n");
    }
    else if (mh->type == ALLOCATED) {
        printf("---   TYPE :    ALLOCATED ---\n");
    }
    printf("---   NEXT : %12p ---\n", mh->next);
    printf("-----------------------------\n");

}

void Mem_MemoryPrint () {
    
    printf("\n");
    printf("-----------------------------\n");
    printf("--------- Memory ------------\n");
    printf("-----------------------------\n");

    memory_head* elt = mm->first;
    
    while (elt != NULL) {
        Mem_MemoryHeadPrint(elt);
        elt = elt->next;
    }
    printf("\n");
}
