;----------------------------------------
; CMOS/RTC helpers (x86_64)
; Read CMOS register
; IN : AL = register number
; OUT: AL = value
;----------------------------------------

BITS 64

global read_cmos
global wait_rtc
global bcd_to_bin
global get_time

read_cmos:
    push rdx

    mov dx, 0x70
    out dx, al

    mov dx, 0x71
    in  al, dx

    pop rdx
    ret

;----------------------------------------
; Wait until RTC is not updating
;----------------------------------------

wait_rtc:
.wait:
    mov al, 0x0A
    call read_cmos
    test al, 0x80
    jnz .wait
    ret

;----------------------------------------
; Convert BCD in AL to binary
;----------------------------------------

bcd_to_bin:
    push rbx

    mov ah, al
    and ah, 0x0F        ; ones
    shr al, 4           ; tens
    mov bl, 10
    mul bl              ; AX = tens * 10
    add al, ah

    pop rbx
    ret

;----------------------------------------
; Get system time
;
; Returns:
;   CH = hour
;   CL = minute
;   DH = second
;----------------------------------------

get_time:
    push rax

    call wait_rtc

    ; Seconds
    mov al, 0x00
    call read_cmos
    call bcd_to_bin
    mov dh, al

    ; Minutes
    mov al, 0x02
    call read_cmos
    call bcd_to_bin
    mov cl, al

    ; Hours
    mov al, 0x04
    call read_cmos
    call bcd_to_bin
    mov ch, al

    pop rax
    ret
