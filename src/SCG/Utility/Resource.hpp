#pragma once
#include <vector>

namespace Sagax::SCG {

    // implementatins should hold their own parameters to use for Init_()
    // Init_() and Dispose_() should be idempotent
    // i reject any allegations claiming similiarity between this interface and StartStop from spire
    struct Resource {
    private:
        bool resourceValid = false;

    public:
        virtual ~Resource() {
            // Dispose(); // cant call this here
        }

        bool Valid() const {
            return resourceValid;
        }

        void Init() {
            if(!resourceValid) {
                Init_();
                resourceValid = true;
            }
        }

        void Dispose() {
            if(resourceValid) {
                Dispose_();
                resourceValid = false;
            }
        }

    protected:
        virtual void Init_() = 0;
        virtual void Dispose_() = 0;
    };

}

