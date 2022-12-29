bits 16
org 0x701
; Main procedure.
; Invoked by the patched MSLOAD with the complete WINBOOT.SYS payload,
; loaded starting at the segment 0x70.
; At entry, we have a valid stack set, and the following register values set:
; AX, BX, DX, DI, BP  - unknown, but signficant, should be restored before passing
; control to the payload.
extension_start:

    mov     cx, 0x70
    mov     ds, cx
    cmp     word [ds:0], 0x5A4C
    je      .compressed

    ; No 'LZ' magic, the payload is not compressed
    jmp     .invoke_winboot

.compressed:
    ; Set up the stack frame.
    push    dx
    push    bx
    push    ax

    cld

    push    ds
    push    cs
    pop     ds

    push    bp ; int 10h AH 0x0E may corrupt BP

    mov     si, copyright
    mov     ah, 0x0E ; Teletype output
    mov     bx, 0x07 ; Page 0, foreground color 7
.next_char:
    lodsb
    test    al, al
    jz      .copyright_done

    int     0x10

    push    cs ; int 10h AH 0x0E may corrupt DS
    pop     ds

    jmp     .next_char
.copyright_done:
    pop     bp

    pop     ds

    mov     si, 2
    ; DS:SI: compressed data

    ; determine where the compressed payload should be relocated to
    lodsw   ; ax - size of the uncompressed payload, in paragraphs
    ; Calculate DI value for WINBOOT.SYS (last DOS paragraph)
    add     ax, 0x60
    push    ax ; this becomes DI when we pass control to WINBOOT

    add     ax, 0x10
    ; relocated compressed data segment
    mov     es, ax
    xor     di, di
    mov     si, 4

    push    ax

    ; DS:SI: relocation source (previous compressed stream location)
    ; ES:DI: relocation target (new compressed stream location)

.copy_next_block:
    lodsw
    stosw
    ; ax - block length
    ; DS:SI: block data

    ; Zero length means last block
    test    ax, ax
    jz      .copy_finished

    mov     cx, ax

    rep     movsb

    call    normalize

    jmp     .copy_next_block

.copy_finished:
    ; The compressed stream has been copied.
    ; ES:DI points beyond the end of it.

    pop     ds

    ; DS:SI - compressed data
    xor     si, si

    ; ES:DI - uncompressed data
    mov     ax, 0x70
    mov     es, ax
    xor     di, di

.uncompress_next_block:
    mov     ax, [si]
    test    ax, ax
    jz      .decompression_finished

    ; Decompress a block from DS:SI to ES:DI.
    call    lz4_decompress_small

    call    normalize

    jmp     .uncompress_next_block

.decompression_finished:
    ; ax is guaranteed zero at this point
    ; some padding at the end of winboot is required for the proper
    ; initialization of it
    mov     cx, 256
    rep     stosw

    pop     di
    pop     ax
    pop     bx
    pop     dx

.invoke_winboot:
    jmp     0x70:0

; Normalizes DS:SI and ES:DI pairs.
normalize:
    mov     ax, si
    shr     ax, 4
    mov     bx, ds
    add     ax, bx
    mov     ds, ax
    and     si, 0x0F

    mov     bx, es
    mov     dx, di
    shr     dx, 4
    add     bx, dx
    mov     es, bx
    and     di, 0x0F

    ret

copyright: db "LZ4_8088 Copyright Jim Leonard", 10, 13, 0

; Decompresses Y. Collet's LZ4 compressed stream data in 16-bit real mode.
; Optimized for 8088/8086 CPUs.
; Code by Trixter/Hornet (trixter@oldskool.org) on 20130105
; Updated 20190617 -- thanks to Peter Ferrie, Terje Mathsen,
; and Axel Kern for suggestions and improvements!
; Updated 20190630: Fixed an alignment bug in lz4_decompress_small
; Updated 20200314: Speed updates from Pavel Zagrebin

;---------------------------------------------------------------
; function lz4_decompress_small(inb,outb:pointer):word; assembler;
;
; Same as LZ4_Decompress but optimized for size, not speed. Still pretty fast,
; although roughly 30% slower than lz4_decompress and RLE sequences are not
; optimally handled.  Same Input, Output, and Trashes as lz4_decompress.
; Minus the Turbo Pascal preamble/postamble, assembles to 78 bytes.
;---------------------------------------------------------------

; At entry:
; DS:SI - source
; ES:DI - destination
; At exit:
; DS:SI, ES:DI - updated, everything else: destroyed

lz4_decompress_small:
        lodsw                   ;load chunk size low 16-bit word
        xchg    bx,ax           ;BX = size of compressed chunk
        add     bx,si           ;BX = threshold to stop decompression
        xor     ax, ax
.parsetoken:                   ;CX=0 here because of REP at end of loop
        lodsb                   ;grab token to AL
        mov     dx,ax           ;preserve packed token in DX
.copyliterals:
        mov     cx,4            ;set full CX reg to ensure CH is 0
        shr     al,cl           ;unpack upper 4 bits
        call    buildfullcount  ;build full literal count if necessary
.doliteralcopy:                  ;src and dst might overlap so do this by bytes
        rep     movsb           ;if cx=0 nothing happens

;At this point, we might be done; all LZ4 data ends with five literals and the
;offset token is ignored.  If we're at the end of our compressed chunk, stop.

        cmp     si,bx           ;are we at the end of our compressed chunk?
        jae     .done          ;if so, jump to exit; otherwise, process match
.copymatches:
        lodsw                   ;AX = match offset
        xchg    dx,ax           ;AX = packed token, DX = match offset
        and     al,0Fh          ;unpack match length token
        call    buildfullcount  ;build full match count if necessary
.domatchcopy:
        push    ds
        push    si              ;ds:si saved, xchg with ax would destroy ah
        mov     si,di
        sub     si,dx
        push    es
        pop     ds              ;ds:si points at match; es:di points at dest
        add     cx,4            ;minmatch = 4
                                ;Can't use MOVSWx2 because [es:di+1] is unknown
        rep     movsb           ;copy match run if any left
        pop     si
        pop     ds              ;ds:si restored
        jmp     .parsetoken

.done:
        ret

buildfullcount:
                                ;CH has to be 0 here to ensure AH remains 0
        cmp     al,0Fh          ;test if unpacked literal length token is 15?
        xchg    cx,ax           ;CX = unpacked literal length token; flags unchanged
        jne     .builddone       ;if AL was not 15, we have nothing to build
.buildloop:
        lodsb                   ;load a byte
        add     cx,ax           ;add it to the full count
        cmp     al,0FFh         ;was it FF?
        je      .buildloop       ;if so, keep going
.builddone:
        retn

