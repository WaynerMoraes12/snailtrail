function(snailtrail_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /w14265 /w14296 /w14928)
        if(SNAILTRAIL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
            -Woverloaded-virtual -Wunused -Wnull-dereference -Wimplicit-fallthrough
            -Wformat=2)
        if(SNAILTRAIL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

macro(snailtrail_enable_sanitizers)
    if(SNAILTRAIL_SANITIZE)
        if(MSVC)
            add_compile_options(/fsanitize=address)
        else()
            add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                                -fno-sanitize-recover=all)
            add_link_options(-fsanitize=address,undefined)
        endif()
    endif()
endmacro()
